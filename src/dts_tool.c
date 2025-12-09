#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/stat.h>
#include <ctype.h>

#define MAX_LINE 4096
#define MAX_BLOCK 131072 // Increased to 128KB
#define DIR_NAME "dtbo_dts"
#define TARGET_PANEL "qcom,mdss_dsi_panel_AE084_P_3_A0033_dsc_cmd_dvt02"

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
            int in_target_panel = 0;
            int panel_brace_depth = 0;

            // Deduplication (Simple array for FPS)
            static int seen_fps[512];
            static int seen_count = 0;
            if (first) {
                memset(seen_fps, 0, sizeof(seen_fps));
                seen_count = 0;
            }

            while (fgets(line, sizeof(line), fp)) {
                // Check for Target Panel Block Entry
                if (!in_target_panel && strstr(line, TARGET_PANEL)) {
                    in_target_panel = 1;
                    panel_brace_depth = 0;
                }

                // Track Panel Block Braces
                if (in_target_panel) {
                    char *p = line;
                    while (*p) {
                        if (*p == '{') panel_brace_depth++;
                        if (*p == '}') panel_brace_depth--;
                        p++;
                    }
                    
                    // If we closed the panel block
                    if (panel_brace_depth <= 0 && strstr(line, "};")) {
                        in_target_panel = 0;
                    }
                }

                // Only scan timings if inside target panel
                if (in_target_panel) {
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
                            // Filter 1: Must be WQHD (hide 1080p/others)
                            if (strstr(current_node, "wqhd")) {
                                // Filter 2: Deduplicate by FPS
                                int is_duplicate = 0;
                                for (int i = 0; i < seen_count; i++) {
                                    if (seen_fps[i] == (int)fps) {
                                        is_duplicate = 1;
                                        break;
                                    }
                                }

                                if (!is_duplicate) {
                                    if (!first) printf(",\n");
                                    printf("  {\"file\": \"%s\", \"node\": \"%s\", \"fps\": %llu, \"clock\": %llu, \"transfer\": %llu}",
                                           dir->d_name, current_node, fps, clock, transfer);
                                    first = 0;
                                    
                                    if (seen_count < 512) {
                                        seen_fps[seen_count++] = (int)fps;
                                    }
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
            int brace_depth = 0; // Tracks depth within the node being skipped
            int modified = 0;
            
            // Scope filtering
            int in_target_panel = 0;
            int panel_brace_depth = 0;

            while (fgets(line, sizeof(line), in)) {
                // Check for Target Panel Block Entry
                if (!in_target_panel && strstr(line, TARGET_PANEL)) {
                    in_target_panel = 1;
                    panel_brace_depth = 0;
                }

                // Track Panel Block Braces
                if (in_target_panel) {
                    char *p = line;
                    while (*p) {
                        if (*p == '{') panel_brace_depth++;
                        if (*p == '}') panel_brace_depth--;
                        p++;
                    }
                    // Check if we are closing the panel
                    // Note: If we are skipping, we shouldn't process panel closure 
                    // until we finish skipping the node (conceptually), 
                    // but usually node closes before panel closes.
                    if (panel_brace_depth <= 0 && strstr(line, "};")) {
                        in_target_panel = 0;
                    }
                }

                if (!skipping) {
                    int match = 0;
                    // Only look for node if inside target panel
                    if (in_target_panel) {
                        // Simple robust check: Line contains target_node
                        char *loc = strstr(line, target_node);
                        if (loc) {
                            // Verify it's not a partial match (e.g. matching "rate_1" in "rate_10")
                            // Check character after node name
                            char *after = loc + strlen(target_node);
                            int is_valid_end = 0;
                            
                            // Skip whitespace
                            while (*after && isspace((unsigned char)*after)) after++;
                            
                            // Must be '{', ';', or end of line (unlikely for node start but possible)
                            if (*after == '{' || *after == '\0' || *after == '\r' || *after == '\n') {
                                is_valid_end = 1;
                            }
                            
                            // Also check character before (to avoid matching "my_rate_1" when looking for "rate_1")
                            // "timing@" is usually the prefix.
                            if (is_valid_end) {
                                match = 1;
                            }
                        }
                    }

                    if (match) {
                        skipping = 1;
                        brace_depth = 0;
                        
                        // Count braces in this line to initialize depth
                        char *p = line;
                        while (*p) {
                            if (*p == '{') brace_depth++;
                            if (*p == '}') brace_depth--;
                            p++;
                        }
                        
                        // If it's a one-liner like "node { ... };", brace_depth might go back to 0
                        if (brace_depth <= 0 && strstr(line, "};")) {
                            skipping = 0; // Finished immediately
                        }
                        
                        modified = 1;
                        printf("Removing node: %s from %s\n", target_node, dir->d_name);
                        continue; // Don't write this line
                    }
                } else {
                    // We are skipping
                    char *p = line;
                    while (*p) {
                        if (*p == '{') brace_depth++;
                        if (*p == '}') brace_depth--;
                        p++;
                    }
                    
                    // Check if node is closed
                    if (brace_depth <= 0 && strstr(line, "};")) {
                        skipping = 0;
                    }
                    continue; // Don't write this line
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

// ---- Command: ADD ----
void cmd_add(const char *base_node, int target_fps) {
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
            if (!in) continue;

            // 1. Capture Base Node Content & Scan for conflicts/index
            char base_content[MAX_BLOCK] = "";
            unsigned long long base_clock = 0;
            unsigned long long base_transfer = 0;
            unsigned long long base_fps = 0;
            
            char line[MAX_LINE];
            int capturing = 0;
            int brace_depth = 0;
            int found_base = 0;
            
            // Scope filtering
            int in_target_panel = 0;
            int panel_brace_depth = 0;

            // New: Track max cell-index and existence
            int max_cell_index = -1;
            int target_exists = 0;
            char target_node_name[128];
            sprintf(target_node_name, "timing@wqhd_sdc_%d", target_fps);

            while (fgets(line, sizeof(line), in)) {
                // Check for Target Panel Block Entry
                if (!in_target_panel && strstr(line, TARGET_PANEL)) {
                    in_target_panel = 1;
                    panel_brace_depth = 0;
                }

                // Track Panel Block Braces
                if (in_target_panel) {
                    char *p = line;
                    while (*p) {
                        if (*p == '{') panel_brace_depth++;
                        if (*p == '}') panel_brace_depth--;
                        p++;
                    }
                    if (panel_brace_depth <= 0 && strstr(line, "};")) {
                        in_target_panel = 0;
                    }
                }
                
                // Only scan inside target panel
                if (in_target_panel) {
                    // Check for max cell-index
                    if (strstr(line, "cell-index")) {
                        char *v = strchr(line, '<');
                        if (v) {
                            int val = (int)parse_hex_or_dec(v + 1);
                            if (val > max_cell_index) max_cell_index = val;
                        }
                    }

                    // Check if target node already exists
                    if (strstr(line, "timing@")) {
                         // Robust existence check
                         char *loc = strstr(line, target_node_name);
                         if (loc) {
                             char *after = loc + strlen(target_node_name);
                             while (*after && isspace((unsigned char)*after)) after++;
                             if (*after == '{' || *after == '\0' || *after == '\r' || *after == '\n') {
                                 target_exists = 1;
                             }
                         }
                    }

                    // Match base node to capture
                    if (strstr(line, base_node)) {
                        // Robust base match
                        char *loc = strstr(line, base_node);
                         if (loc) {
                             char *after = loc + strlen(base_node);
                             while (*after && isspace((unsigned char)*after)) after++;
                             if (*after == '{' || *after == '\0' || *after == '\r' || *after == '\n') {
                                 capturing = 1;
                                 found_base = 1;
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
                printf("Error: Target node %s already exists in %s\n", target_node_name, dir->d_name);
                fclose(in);
                remove(temp_path);
                continue;
            }

            if (!found_base) {
                printf("Error: Base node %s not found in %s\n", base_node, dir->d_name);
                fclose(in);
                remove(temp_path);
                continue;
            }

            // 2. Write new file with appended node
            FILE *out = fopen(temp_path, "w");
            if (!out) { fclose(in); continue; }

            int inserted = 0;
            // Reset scope vars for second pass
            in_target_panel = 0;
            panel_brace_depth = 0;
            
            while (fgets(line, sizeof(line), in)) {
                // Check for Target Panel Block Entry
                if (!in_target_panel && strstr(line, TARGET_PANEL)) {
                    in_target_panel = 1;
                    panel_brace_depth = 0;
                }

                // Track Panel Block Braces
                if (in_target_panel) {
                    char *p = line;
                    while (*p) {
                        if (*p == '{') panel_brace_depth++;
                        if (*p == '}') panel_brace_depth--;
                        p++;
                    }
                    if (panel_brace_depth <= 0 && strstr(line, "};")) {
                        in_target_panel = 0;
                    }
                }

                fputs(line, out);
                
                // Only match if inside target panel
                // We use exact match logic again to find where to insert (after base node)
                if (!inserted && in_target_panel && strstr(line, base_node)) {
                     // Verify match again to be safe
                     char *loc = strstr(line, base_node);
                     int is_match = 0;
                     if (loc) {
                         char *after = loc + strlen(base_node);
                         while (*after && isspace((unsigned char)*after)) after++;
                         if (*after == '{' || *after == '\0' || *after == '\r' || *after == '\n') {
                             is_match = 1;
                         }
                     }

                     if (is_match) {
                        int inner_depth = 0;
                        char *p = line;
                        while (*p) {
                            if (*p == '{') inner_depth++;
                            if (*p == '}') inner_depth--;
                            p++;
                        }

                        char inner_line[MAX_LINE];
                        while(inner_depth > 0 && fgets(inner_line, sizeof(inner_line), in)) {
                             fputs(inner_line, out);
                             char *p2 = inner_line;
                             while (*p2) {
                                if (*p2 == '{') inner_depth++;
                                if (*p2 == '}') inner_depth--;
                                p2++;
                             }
                        }
                        
                        // Now we are after the base node. Insert new node.
                        if (base_fps > 0) {
                            unsigned long long new_clock = base_clock * target_fps / base_fps;
                            unsigned long long new_transfer = base_transfer * base_fps / target_fps;
                            
                            char new_node[MAX_BLOCK];
                            strcpy(new_node, base_content);
                            
                            char buf[128];
                            
                            // Replace Name
                            char old_name_clean[128];
                            char *nm_end = strpbrk(base_node, " {");
                            int nm_len = nm_end ? (nm_end - base_node) : strlen(base_node);
                            strncpy(old_name_clean, base_node, nm_len);
                            old_name_clean[nm_len] = '\0';

                            // target_node_name was calculated earlier
                            replace_str(new_node, old_name_clean, target_node_name);

                            // Replace FPS
                            sprintf(buf, "qcom,mdss-dsi-panel-framerate = <0x%x>;", target_fps);
                            char *fps_loc = strstr(new_node, "qcom,mdss-dsi-panel-framerate");
                            if (fps_loc) {
                                char *line_end = strchr(fps_loc, ';');
                                if (line_end) {
                                    char prefix[MAX_BLOCK], suffix[MAX_BLOCK];
                                    strncpy(prefix, new_node, fps_loc - new_node);
                                    prefix[fps_loc - new_node] = '\0';
                                    strcpy(suffix, line_end + 1);
                                    sprintf(new_node, "%s%s%s", prefix, buf, suffix);
                                }
                            }

                            // Replace Clock
                            sprintf(buf, "qcom,mdss-dsi-panel-clockrate = <0x%llx>;", new_clock);
                            char *clk_loc = strstr(new_node, "qcom,mdss-dsi-panel-clockrate");
                            if (clk_loc) {
                                char *line_end = strchr(clk_loc, ';');
                                if (line_end) {
                                    char prefix[MAX_BLOCK], suffix[MAX_BLOCK];
                                    strncpy(prefix, new_node, clk_loc - new_node);
                                    prefix[clk_loc - new_node] = '\0';
                                    strcpy(suffix, line_end + 1);
                                    sprintf(new_node, "%s%s%s", prefix, buf, suffix);
                                }
                            }

                            // Replace Transfer Time
                            sprintf(buf, "qcom,mdss-mdp-transfer-time-us = <0x%llx>;", new_transfer);
                            char *tr_loc = strstr(new_node, "qcom,mdss-mdp-transfer-time-us");
                            if (tr_loc) {
                                char *line_end = strchr(tr_loc, ';');
                                if (line_end) {
                                    char prefix[MAX_BLOCK], suffix[MAX_BLOCK];
                                    strncpy(prefix, new_node, tr_loc - new_node);
                                    prefix[tr_loc - new_node] = '\0';
                                    strcpy(suffix, line_end + 1);
                                    sprintf(new_node, "%s%s%s", prefix, buf, suffix);
                                }
                            }
                            
                            // Replace cell-index with max_index + 1
                            int new_index = max_cell_index + 1;
                            // Prefer hex format 0x...
                            sprintf(buf, "cell-index = <0x%x>;", new_index);
                            char *idx_loc = strstr(new_node, "cell-index");
                            if (idx_loc) {
                                 char *line_end = strchr(idx_loc, ';');
                                if (line_end) {
                                    char prefix[MAX_BLOCK], suffix[MAX_BLOCK];
                                    strncpy(prefix, new_node, idx_loc - new_node);
                                    prefix[idx_loc - new_node] = '\0';
                                    strcpy(suffix, line_end + 1);
                                    sprintf(new_node, "%s%s%s", prefix, buf, suffix);
                                }
                            }

                            fprintf(out, "\n%s\n", new_node);
                            printf("Added %dHz node (index %d) to %s\n", target_fps, new_index, dir->d_name);
                        }
                        inserted = 1;
                     }
                }
            }
            fclose(in);
            fclose(out);
            remove(path);
            rename(temp_path, path);
        }
        closedir(d);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <scan|remove|add> [args...]\n", argv[0]);
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
    }

    return 0;
}
