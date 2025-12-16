#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

#define MAX_LINE 4096
#define MAX_BLOCK 131072 // 128KB
#define DIR_NAME "dtbo_dts"

// Utils
int is_regular_file(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);
    return S_ISREG(path_stat.st_mode);
}

void replace_str(char *str, const char *orig, const char *rep) {
    char buffer[MAX_BLOCK];
    char *p;
    if (!(p = strstr(str, orig))) return;
    strncpy(buffer, str, p - str);
    buffer[p - str] = '\0';
    sprintf(buffer + (p - str), "%s%s", rep, p + strlen(orig));
    strcpy(str, buffer);
}

unsigned long long parse_hex_or_dec(const char *str) {
    if (strstr(str, "0x") || strstr(str, "0X")) {
        return strtoull(str, NULL, 16);
    }
    return strtoull(str, NULL, 10);
}

// Helper: Check if line starts a panel definition
// Matches "qcom,mdss_dsi_panel_..."
int is_panel_start(const char *line) {
    // Explicitly ignore engineering panels
    if (strstr(line, "_evt")) return 0;

    if (strstr(line, "qcom,mdss_dsi_panel_")) return 1;
    // Also support hyphens just in case
    if (strstr(line, "qcom,mdss-dsi-panel-")) return 1;
    return 0;
}

// ---- Command: SCAN ----
void cmd_scan() {
    DIR *d;
    struct dirent *dir;
    char path[512];
    FILE *fp;
    char line[MAX_LINE];
    
    printf("[\n");
    int first = 1;

    d = opendir(DIR_NAME);
    if (!d) {
        d = opendir(".");
    }

    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (!strstr(dir->d_name, ".dts")) continue;
            
            snprintf(path, sizeof(path), "%s/%s", DIR_NAME, dir->d_name);
            if (access(path, F_OK) != 0) snprintf(path, sizeof(path), "%s", dir->d_name);

            fp = fopen(path, "r");
            if (!fp) continue;

            char current_node[256] = "";
            unsigned long long fps = 0;
            unsigned long long clock = 0;
            unsigned long long transfer = 0;
            int in_timing = 0;
            int timing_brace_depth = 0;
            
            // Filtering vars
            int in_panel = 0;
            int panel_brace_depth = 0;

            // Deduplication (Simple array for FPS)
            static int seen_fps[512];
            static int seen_count = 0;
            if (first) {
                memset(seen_fps, 0, sizeof(seen_fps));
                seen_count = 0;
            }

            while (fgets(line, sizeof(line), fp)) {
                // Check for Panel Block Entry
                if (!in_panel && is_panel_start(line)) {
                    in_panel = 1;
                    panel_brace_depth = 0;
                }

                // Track Panel Block Braces
                if (in_panel) {
                    char *p = line;
                    while (*p) {
                        if (*p == '{') panel_brace_depth++;
                        if (*p == '}') panel_brace_depth--;
                        p++;
                    }
                    
                    // If we closed the panel block
                    if (panel_brace_depth <= 0 && strstr(line, "};")) {
                        in_panel = 0;
                    }
                }

                // Only scan timings if inside panel
                if (in_panel) {
                    if (strstr(line, "timing@")) {
                        char *start = strstr(line, "timing@");
                        char *end = start + strlen("timing@");
                        
                        // Find end of node name (space, {, or newline)
                        while (*end && !isspace((unsigned char)*end) && *end != '{') {
                            end++;
                        }
                        
                        if (end > start) {
                            int len = end - start;
                            if (len >= sizeof(current_node)) len = sizeof(current_node) - 1;
                            strncpy(current_node, start, len);
                            current_node[len] = '\0';
                            
                            in_timing = 1;
                            timing_brace_depth = 0;
                            fps = 0; clock = 0; transfer = 0;
                        }
                    }

                    if (in_timing) {
                        char *p = line;
                        while (*p) {
                            if (*p == '{') timing_brace_depth++;
                            if (*p == '}') timing_brace_depth--;
                            p++;
                        }

                        char *val_start;
                        if ((val_start = strstr(line, "qcom,mdss-dsi-panel-framerate"))) {
                            char *v = strchr(val_start, '<');
                            if (v) fps = parse_hex_or_dec(v + 1);
                        }
                        if ((val_start = strstr(line, "qcom,mdss-dsi-panel-clockrate"))) {
                            char *v = strchr(val_start, '<');
                            if (v) clock = parse_hex_or_dec(v + 1);
                        }
                        if ((val_start = strstr(line, "qcom,mdss-mdp-transfer-time-us"))) {
                            char *v = strchr(val_start, '<');
                            if (v) transfer = parse_hex_or_dec(v + 1);
                        }

                        if (timing_brace_depth == 0 && strstr(line, "};")) {
                            // Filter 1: Must be WQHD (hide 1080p/others) - Relaxed to show all for multi-model
                            // Actually, let's keep it simple: Show all timing nodes found.
                            
                            // Filter 2: Deduplicate by FPS
                            int is_duplicate = 0;
                            for (int i = 0; i < seen_count; i++) {
                                if (seen_fps[i] == (int)fps) {
                                    is_duplicate = 1;
                                    break;
                                }
                            }

                            if (!is_duplicate && fps > 0) {
                                if (!first) printf(",\n");
                                printf("  {\"file\": \"%s\", \"node\": \"%s\", \"fps\": %llu, \"clock\": %llu, \"transfer\": %llu}",
                                       dir->d_name, current_node, fps, clock, transfer);
                                first = 0;
                                
                                if (seen_count < 512) {
                                    seen_fps[seen_count++] = (int)fps;
                                }
                            }
                            
                            in_timing = 0;
                        }
                    }
                }
            }
            fclose(fp);
        }
        closedir(d);
    }
    printf("\n]\n");
}

// ---- Command: REMOVE ----
void cmd_remove(const char *target_node) {
    DIR *d;
    struct dirent *dir;
    char path[512];
    char temp_path[512];
    
    d = opendir(DIR_NAME);
    if (!d) d = opendir(".");

    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (!strstr(dir->d_name, ".dts")) continue;
            
            snprintf(path, sizeof(path), "%s/%s", DIR_NAME, dir->d_name);
            if (access(path, F_OK) != 0) snprintf(path, sizeof(path), "%s", dir->d_name);
            
            snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

            FILE *in = fopen(path, "r");
            FILE *out = fopen(temp_path, "w");
            if (!in || !out) continue;

            char line[MAX_LINE];
            int skipping = 0;
            int brace_depth = 0; 
            int modified = 0;
            
            // Scope filtering
            int in_panel = 0;
            int panel_brace_depth = 0;

            while (fgets(line, sizeof(line), in)) {
                if (!in_panel && is_panel_start(line)) {
                    in_panel = 1;
                    panel_brace_depth = 0;
                }

                if (in_panel) {
                    char *p = line;
                    while (*p) {
                        if (*p == '{') panel_brace_depth++;
                        if (*p == '}') panel_brace_depth--;
                        p++;
                    }
                    if (panel_brace_depth <= 0 && strstr(line, "};")) {
                        in_panel = 0;
                    }
                }

                if (!skipping) {
                    int match = 0;
                    if (in_panel) {
                        char *loc = strstr(line, target_node);
                        if (loc) {
                            char *after = loc + strlen(target_node);
                            int is_valid_end = 0;
                            while (*after && isspace((unsigned char)*after)) after++;
                            if (*after == '{' || *after == '\0' || *after == '\r' || *after == '\n') {
                                is_valid_end = 1;
                            }
                            if (is_valid_end) {
                                match = 1;
                            }
                        }
                    }

                    if (match) {
                        skipping = 1;
                        brace_depth = 0;
                        char *p = line;
                        while (*p) {
                            if (*p == '{') brace_depth++;
                            if (*p == '}') brace_depth--;
                            p++;
                        }
                        if (brace_depth <= 0 && strstr(line, "};")) {
                            skipping = 0; 
                        }
                        modified = 1;
                        printf("Removing node: %s from %s\n", target_node, dir->d_name);
                        continue; 
                    }
                } else {
                    char *p = line;
                    while (*p) {
                        if (*p == '{') brace_depth++;
                        if (*p == '}') brace_depth--;
                        p++;
                    }
                    if (brace_depth <= 0 && strstr(line, "};")) {
                        skipping = 0;
                    }
                    continue; 
                }

                fputs(line, out);
            }

            fclose(in);
            fclose(out);

            if (modified) {
                remove(path); 
                rename(temp_path, path);
            } else {
                remove(temp_path);
            }
        }
        closedir(d);
    }
}

// ---- Command: ADD (Internal) ----
void internal_add_node(const char *filename, const char *base_node, int target_fps) {
    char path[512];
    char temp_path[512];
    snprintf(path, sizeof(path), "%s/%s", DIR_NAME, filename);
    if (access(path, F_OK) != 0) snprintf(path, sizeof(path), "%s", filename);
    snprintf(temp_path, sizeof(temp_path), "%s.tmp", path);

    FILE *in = fopen(path, "r");
    if (!in) return;

    // 1. Capture Base Node Content
    char base_content[MAX_BLOCK] = "";
    unsigned long long base_clock = 0;
    unsigned long long base_transfer = 0;
    unsigned long long base_fps = 0;
    
    char line[MAX_LINE];
    int capturing = 0;
    int brace_depth = 0;
    int found_base = 0;
    
    int in_panel = 0;
    int panel_brace_depth = 0;

    int max_cell_index = -1;
    int target_exists = 0;
    char target_node_name[128];
    sprintf(target_node_name, "timing@wqhd_sdc_%d", target_fps); // Default naming, can be adjusted
    // Note: The base_node might have a different prefix. We should reuse base_node's prefix if possible.
    // For now, hardcoded wqhd_sdc is risky for multi-model.
    // Better: Derive name from base_node name.

    while (fgets(line, sizeof(line), in)) {
        if (!in_panel && is_panel_start(line)) {
            in_panel = 1;
            panel_brace_depth = 0;
        }

        if (in_panel) {
            char *p = line;
            while (*p) {
                if (*p == '{') panel_brace_depth++;
                if (*p == '}') panel_brace_depth--;
                p++;
            }
            if (panel_brace_depth <= 0 && strstr(line, "};")) {
                in_panel = 0;
            }
        }
        
        if (in_panel) {
            if (strstr(line, "cell-index")) {
                char *v = strchr(line, '<');
                if (v) {
                    int val = (int)parse_hex_or_dec(v + 1);
                    if (val > max_cell_index) max_cell_index = val;
                }
            }

            // Check if target node already exists (approximate check)
            if (strstr(line, "timing@") && strstr(line, target_node_name)) {
                 target_exists = 1;
            }

            if (strstr(line, base_node)) {
                char *loc = strstr(line, base_node);
                 if (loc) {
                     char *after = loc + strlen(base_node);
                     while (*after && isspace((unsigned char)*after)) after++;
                     if (*after == '{' || *after == '\0' || *after == '\r' || *after == '\n') {
                         capturing = 1;
                         found_base = 1;
                         
                         // Fix target node name to match base node pattern
                         // e.g. base: timing@sdc_fhd_120 -> target: timing@sdc_fhd_123
                         // We replace the last number.
                         strcpy(target_node_name, base_node);
                         char *last_underscore = strrchr(target_node_name, '_');
                         if (last_underscore) {
                             sprintf(last_underscore + 1, "%d", target_fps);
                         } else {
                             // Fallback
                             sprintf(target_node_name, "%s_%d", base_node, target_fps);
                         }
                     }
                 }
            }
        }
        
        if (capturing) {
            if (strlen(base_content) + strlen(line) < MAX_BLOCK) {
                strcat(base_content, line);
            }
            
            char *p = line;
            while (*p) {
                if (*p == '{') brace_depth++;
                if (*p == '}') brace_depth--;
                p++;
            }

            char *val;
            if ((val = strstr(line, "qcom,mdss-dsi-panel-framerate"))) {
                char *v = strchr(val, '<'); if(v) base_fps = parse_hex_or_dec(v+1);
            }
            if ((val = strstr(line, "qcom,mdss-dsi-panel-clockrate"))) {
                char *v = strchr(val, '<'); if(v) base_clock = parse_hex_or_dec(v+1);
            }
            if ((val = strstr(line, "qcom,mdss-mdp-transfer-time-us"))) {
                char *v = strchr(val, '<'); if(v) base_transfer = parse_hex_or_dec(v+1);
            }

            if (brace_depth <= 0 && strstr(line, "};")) {
                capturing = 0;
            }
        }
    }
    rewind(in); 

    if (target_exists) {
        printf("Skipping: %s already exists in %s\n", target_node_name, filename);
        fclose(in);
        remove(temp_path);
        return;
    }

    if (!found_base) {
        printf("Error: Base node %s not found in %s\n", base_node, filename);
        fclose(in);
        remove(temp_path);
        return;
    }

    // 2. Write new file with appended node
    FILE *out = fopen(temp_path, "w");
    if (!out) { fclose(in); return; }

    int inserted = 0;
    in_panel = 0;
    panel_brace_depth = 0;
    
    while (fgets(line, sizeof(line), in)) {
        if (!in_panel && is_panel_start(line)) {
            in_panel = 1;
            panel_brace_depth = 0;
        }

        if (in_panel) {
            char *p = line;
            while (*p) {
                if (*p == '{') panel_brace_depth++;
                if (*p == '}') panel_brace_depth--;
                p++;
            }
            if (panel_brace_depth <= 0 && strstr(line, "};")) {
                in_panel = 0;
            }
        }

        fputs(line, out);
        
        // Insert AFTER the base node block is fully written (not implemented here easily)
        // Alternative: Insert at the END of the panel block (safest)
        
        // Check if we are closing the panel block
        if (!inserted && !in_panel && strstr(line, "};") && found_base) {
            // Wait, we need to insert BEFORE the panel closing brace "};"
            // The logic above prints the line, so we are outside now?
            // Actually, we should check *before* writing the line if it closes the panel.
            // But simplifying: just use sed-like logic or process carefully?
            // Let's rely on standard logic:
            // We printed "};" just now. That's wrong. We need to insert before it.
        }
    }
    
    // Re-do write logic to insert correctly
    fclose(out);
    fclose(in);
    
    // Quick Hack: Re-open and use memory buffer or simple insertion
    // Since we are C, let's just do it properly in the loop.
    
    in = fopen(path, "r");
    out = fopen(temp_path, "w");
    
    in_panel = 0;
    panel_brace_depth = 0;
    inserted = 0;

    while (fgets(line, sizeof(line), in)) {
        int closing_panel = 0;

        if (!in_panel && is_panel_start(line)) {
            in_panel = 1;
            panel_brace_depth = 0;
        }

        if (in_panel) {
            char *p = line;
            while (*p) {
                if (*p == '{') panel_brace_depth++;
                if (*p == '}') panel_brace_depth--;
                p++;
            }
            if (panel_brace_depth <= 0 && strstr(line, "};")) {
                closing_panel = 1;
                in_panel = 0;
            }
        }

        if (closing_panel && !inserted && found_base) {
            // Generate New Node Content
            replace_str(base_content, base_node, target_node_name);
            
            // Calc new values
            if (base_fps > 0) {
                unsigned long long new_clock = base_clock * target_fps / base_fps;
                unsigned long long new_transfer = base_transfer * base_fps / target_fps;
                
                char old_val[64], new_val[64];
                
                sprintf(old_val, "<%llu>", base_clock);
                sprintf(new_val, "<%llu>", new_clock);
                replace_str(base_content, old_val, new_val);
                
                sprintf(old_val, "<%llu>", base_fps);
                sprintf(new_val, "<0x%x>", target_fps); // Hex for fps
                replace_str(base_content, old_val, new_val);
                
                sprintf(old_val, "<%llu>", base_transfer);
                sprintf(new_val, "<%llu>", new_transfer);
                replace_str(base_content, old_val, new_val);
                
                // Update cell-index
                sprintf(old_val, "cell-index = <");
                char *ci = strstr(base_content, old_val);
                if (ci) {
                    // Find actual value start
                    char *val_start = strchr(ci, '<');
                    if (val_start) {
                        char *val_end = strchr(val_start, '>');
                        if (val_end) {
                            // Reconstruct string with new index
                            *val_end = '\0'; // Temporarily terminate
                            char prefix[MAX_BLOCK];
                            strncpy(prefix, base_content, val_start - base_content + 1);
                            prefix[val_start - base_content + 1] = '\0';
                            
                            char suffix[MAX_BLOCK];
                            strcpy(suffix, val_end + 1);
                            
                            sprintf(base_content, "%s%d>%s", prefix, max_cell_index + 1, suffix);
                        }
                    }
                }
            }
            
            fprintf(out, "\n%s", base_content);
            inserted = 1;
            printf("Added node %s (%dHz) to %s\n", target_node_name, target_fps, filename);
        }

        fputs(line, out);
    }
    
    fclose(in);
    fclose(out);
    
    remove(path);
    rename(temp_path, path);
}


void cmd_add(const char *base_node, int target_fps) {
    DIR *d;
    struct dirent *dir;
    d = opendir(DIR_NAME);
    if (!d) d = opendir(".");
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (!strstr(dir->d_name, ".dts")) continue;
            internal_add_node(dir->d_name, base_node, target_fps);
        }
        closedir(d);
    }
}

// ---- Command: SMART ADD ----
void cmd_smart_add(int target_fps) {
    DIR *d;
    struct dirent *dir;
    d = opendir(DIR_NAME);
    if (!d) d = opendir(".");
    
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            if (!strstr(dir->d_name, ".dts")) continue;
            
            char path[512];
            snprintf(path, sizeof(path), "%s/%s", DIR_NAME, dir->d_name);
            if (access(path, F_OK) != 0) snprintf(path, sizeof(path), "%s", dir->d_name);
            
            FILE *fp = fopen(path, "r");
            if (!fp) continue;

            char best_node[256] = "";
            unsigned long long max_fps = 0;
            
            char line[MAX_LINE];
            int in_panel = 0;
            int panel_brace_depth = 0;
            int in_timing = 0;
            int timing_brace_depth = 0;
            char current_node[256] = "";
            unsigned long long current_fps = 0;

            // 1. Scan for Max FPS Node
            while (fgets(line, sizeof(line), fp)) {
                if (!in_panel && is_panel_start(line)) {
                    in_panel = 1;
                    panel_brace_depth = 0;
                }

                if (in_panel) {
                    char *p = line;
                    while (*p) {
                        if (*p == '{') panel_brace_depth++;
                        if (*p == '}') panel_brace_depth--;
                        p++;
                    }
                    if (panel_brace_depth <= 0 && strstr(line, "};")) {
                        in_panel = 0;
                    }

                    if (strstr(line, "timing@")) {
                        char *start = strstr(line, "timing@");
                        char *end = start + strlen("timing@");
                        while (*end && !isspace((unsigned char)*end) && *end != '{') end++;
                        if (end > start) {
                            int len = end - start;
                            if (len >= sizeof(current_node)) len = sizeof(current_node) - 1;
                            strncpy(current_node, start, len);
                            current_node[len] = '\0';
                            in_timing = 1;
                            timing_brace_depth = 0;
                            current_fps = 0;
                        }
                    }

                    if (in_timing) {
                        char *p = line;
                        while (*p) {
                            if (*p == '{') timing_brace_depth++;
                            if (*p == '}') timing_brace_depth--;
                            p++;
                        }
                        
                        char *val_start;
                        if ((val_start = strstr(line, "qcom,mdss-dsi-panel-framerate"))) {
                            char *v = strchr(val_start, '<');
                            if (v) current_fps = parse_hex_or_dec(v + 1);
                        }

                        if (timing_brace_depth == 0 && strstr(line, "};")) {
                            if (current_fps > max_fps) {
                                max_fps = current_fps;
                                strcpy(best_node, current_node);
                            }
                            in_timing = 0;
                        }
                    }
                }
            }
            fclose(fp);
            
            // 2. Add Node if Base Found
            if (max_fps > 0 && strlen(best_node) > 0) {
                printf("Found best base node: %s (%llu Hz) in %s\n", best_node, max_fps, dir->d_name);
                internal_add_node(dir->d_name, best_node, target_fps);
            } else {
                printf("No suitable base node found in %s\n", dir->d_name);
            }
        }
        closedir(d);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <scan|remove|add|smart_add> [args]\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "scan") == 0) {
        cmd_scan();
    } else if (strcmp(argv[1], "remove") == 0) {
        if (argc < 3) return 1;
        cmd_remove(argv[2]);
    } else if (strcmp(argv[1], "add") == 0) {
        if (argc < 4) return 1;
        cmd_add(argv[2], atoi(argv[3]));
    } else if (strcmp(argv[1], "smart_add") == 0) {
        if (argc < 3) return 1;
        cmd_smart_add(atoi(argv[2]));
    } else {
        printf("Unknown command\n");
        return 1;
    }

    return 0;
}
