#!/system/bin/sh
MODDIR=${0%/*}
DAEMON_BIN="$MODDIR/bin/rate_daemon"

# 等待系统启动完成
until [ "$(getprop sys.boot_completed)" = "1" ]; do
    sleep 1
done

# 启动守护进程
# 传入模块路径作为参数
chmod +x "$DAEMON_BIN"
nohup "$DAEMON_BIN" "$MODDIR" > /dev/null 2>&1 &
