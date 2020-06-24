#!/system/bin/sh
# SPECTRUM & FRANCO KERNEL MANAGERS
# Profile initialization by Zile995

# helper functions to allow Android init like script
function write() {
    echo -n $2 > $1
}

#Cpu Governor And Frequencies Scaling Settings
echo 0 > /sys/devices/system/cpu/cpu3/online
echo 0 > /sys/devices/system/cpu/cpu5/online
#A53
echo pwrutilx > /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor
echo 400000 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_min_freq
echo 1382400 > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq
echo 990 > /sys/devices/system/cpu/cpu0/cpufreq/pwrutilx/up_rate_limit_us
echo 1000 > /sys/devices/system/cpu/cpu0/cpufreq/pwrutilx/down_rate_limit_us
echo 0 > /sys/devices/system/cpu/cpu0/cpufreq/pwrutilx/iowait_boost_enable
#A72
echo pwrutilx > /sys/devices/system/cpu/cpu4/cpufreq/scaling_governor
echo 400000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_min_freq
echo 400000 > /sys/devices/system/cpu/cpu4/cpufreq/scaling_max_freq
echo 1000 > /sys/devices/system/cpu/cpu4/cpufreq/pwrutilx/up_rate_limit_us
echo 1000 > /sys/devices/system/cpu/cpu4/cpufreq/pwrutilx/down_rate_limit_us
echo 0 > /sys/devices/system/cpu/cpu4/cpufreq/pwrutilx/iowait_boost_enable
#Cpu-Boost
echo 1 > /sys/module/cpu_boost/parameters/input_boost_enabled
echo 0:0 1:0 2:0 3:0 4:0 5:0 > /sys/module/cpu_boost/parameters/input_boost_freq
echo 100 > /sys/module/cpu_boost/parameters/input_boost_ms
echo 0 > /sys/module/cpu_boost/parameters/devfreq_input_boost
echo 10 > /sys/module/cpu_boost/parameters/dynamic_stune_boost
#Frame-Boost
echo N > /sys/module/mdss_mdp/parameters/frame_boost
#Workqueue
echo Y > /sys/module/workqueue/parameters/power_efficient
#Scheduler
echo noop > /sys/block/mmcblk0/queue/scheduler
echo noop > /sys/block/mmcblk1/queue/scheduler 
#Adreno-Driver and Gpu
echo 0 > /sys/class/kgsl/kgsl-3d0/devfreq/adrenoboost
echo N > /sys/module/adreno_idler/parameters/adreno_idler_active
echo 90 > /sys/module/adreno_idler/parameters/adreno_idler_downdifferential
echo 4 > /sys/module/adreno_idler/parameters/adreno_idler_idlewait
echo 9000 > /sys/module/adreno_idler/parameters/adreno_idler_idleworkload
echo 550000000 > /sys/class/kgsl/kgsl-3d0/max_gpuclk
echo 550000000 > /sys/class/kgsl/kgsl-3d0/devfreq/max_freq
echo 266666667 > /sys/class/kgsl/kgsl-3d0/devfreq/min_freq
echo 4 > /sys/devices/soc.0/1c00000.qcom,kgsl-3d0/kgsl/kgsl-3d0/max_pwrlevel
#Devfreq-Drivers
#echo 9887 > /sys/devices/soc.0/qcom,cpubw.50/devfreq/cpubw/max_freq
echo 805 > /sys/devices/soc.0/qcom,cpubw.50/devfreq/cpubw/min_freq
#echo 9887 > /sys/devices/soc.0/qcom,gpubw.36/devfreq/gpubw/max_freq
echo 0 > /sys/devices/soc.0/qcom,gpubw.36/devfreq/gpubw/min_freq
#Walt-Toggles
echo 0 > /proc/sys/kernel/sched_use_walt_cpu_util
echo 0 > /proc/sys/kernel/sched_use_walt_task_util
#ScheduleTuning
echo -50 > /dev/stune/background/schedtune.boost
echo -50 > /dev/stune/foreground/schedtune.boost
echo 1 > /dev/stune/top-app/schedtune.boost
echo 0 > /dev/stune/top-app/schedtune.prefer_idle
#Msm-Thermals
echo 45 > /sys/module/msm_thermal/parameters/temp_threshold
echo 60 > /sys/module/msm_thermal/parameters/core_limit_temp_degC
#Vmpressure
echo 60 > /proc/sys/vm/vfs_cache_pressure
