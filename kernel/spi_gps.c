#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/hrtimer.h>
#include <linux/sched.h>

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/time.h>
#include <linux/delay.h>

#include <linux/device.h>

#include <asm/siginfo.h>	
#include <linux/rcupdate.h>	
#include <linux/uaccess.h>

// GPIO_25 
#define GPIO_IN_GPIO			25
#define GPIO_IN_GPIO_DESC		"GPS input"
#define GPIO_IN_GPIO_DEVICE_DESC    "GPIO interrrupt"

// GPIO_24 
#define GPIO_OUT_GPIO           24
#define GPIO_OUT_GPIO_DESC		"GPS output"

#define	TRUE	1
#define	FALSE	0
#define	ONE_SECOND	1000000000
#define	HALF_SECOND	500000000

#define SIG_TEST 44	// we choose 44 as our signal number (real-time signals are in the range of 33 to 64)

// The structure to get current CPU time 
struct timespec my_real_cpu_time;

// Timer
static struct hrtimer htimer;
static ktime_t kt_periode;

int (*htimer2_event_func)(void);

// Create file /sys/module/spi_gps/parameters/udp_pid
static int sys_udp_pid = 1;
module_param_named(udp_pid, sys_udp_pid, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(udp_pid, "udp port to call back");

// Create file /sys/module/spi_gps/parameters/gps_out_enabled
static int sys_gps_out_enabled = TRUE;
module_param_named(gps_out_enabled, sys_gps_out_enabled, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(gps_out_enabled, "enable/disable gps output");

// Create file /sys/module/spi_gps/parameters/gps_in_enabled
static int sys_gps_in_enabled = TRUE;
module_param_named(gps_in_enabled, sys_gps_in_enabled, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(gps_in_enabled, "enable/disable gps input");

// Create file /sys/module/spi_gps/parameters/gps_gpio_in
static int sys_gps_gpio_in = GPIO_IN_GPIO;
module_param_named(gps_gpio_in, sys_gps_gpio_in, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(gps_gpio_in, "gpio pin to use for the gps input");

// Create file /sys/module/spi_gps/parameters/gps_gpio_out
static int sys_gps_gpio_out = GPIO_OUT_GPIO;
module_param_named(gps_gpio_out, sys_gps_gpio_out, int, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(gps_gpio_out, "gpio pin to use for the gps putput");

short int irq_in_gpio    = 0;

int sys_time_now_sec = 0;
int sys_time_now_nsec = 0;
int sys_gpio_out_level = 1;

int gps_ntp_timeshift = 0;

int spi_snap_time = 200000000;

/***************************************************************************
 * get cpu time
*/
int get_Current_Time (void)
{
	getnstimeofday(&my_real_cpu_time);
	sys_time_now_sec = my_real_cpu_time.tv_sec;
	sys_time_now_nsec = my_real_cpu_time.tv_nsec;
	return TRUE;
}

/***************************************************************************
 * Not in use
*/
int gps_GPIO_Signal_Detector_HiToLow (void)
{
	int wait_counter = 0;
	int level_in = 1;
	
	// Wait for pps signal
	while ((level_in != 0) && (wait_counter < 10000))
	{
		//level_in = gpio_get_value(sys_gps_gpio_in);
		wait_counter ++;
	}
	return TRUE;
}

/***************************************************************************
 * Sending signal to user space
 */
int send_Signal (int pid)
{
	int ret;
	struct siginfo info;
	struct task_struct *t;
	//
	memset(&info, 0, sizeof(struct siginfo));
	info.si_signo = SIG_TEST;
	info.si_code = SI_QUEUE;
	info.si_int = pid; 
	//
	rcu_read_lock();
	t = pid_task(find_pid_ns(pid, &init_pid_ns), PIDTYPE_PID);	
	if(t == NULL)
	{
		printk(KERN_NOTICE "Pid not found \n");
		rcu_read_unlock();
		return FALSE;
	}
	rcu_read_unlock();
	ret = send_sig_info(SIG_TEST, &info, t);    //send the signal
	if (ret < 0)
	{
		printk(KERN_NOTICE "Error sending signal \n");
		return FALSE;
	}
	return TRUE;
}

/***************************************************************************
 * Try to send signal to user space
*/
int user_Space_Request_Processing(void)
{
	
	if (sys_udp_pid != 1)
	{
		send_Signal (sys_udp_pid);
		printk(KERN_NOTICE "Signal sent pid %d \n", sys_udp_pid);
		sys_udp_pid = 1;
	}
	return TRUE;
}

/***************************************************************************
 * Interruption triggered by GPIO_IN
*/
static irqreturn_t r_irq_handler(int irq, void *dev_id, struct pt_regs *regs) {
 
	unsigned long flags;
	int irg_current_time;
   
	// disable hard interrupts (remember them in flag 'flags')
	local_irq_save(flags);
 	get_Current_Time();
	irg_current_time = sys_time_now_nsec;
	if (irg_current_time > HALF_SECOND)
	{
		irg_current_time -= ONE_SECOND;
	}
	gps_ntp_timeshift += (irg_current_time - gps_ntp_timeshift) / 4;
	//
	//printk(KERN_NOTICE "Time shift %d\n", gps_ntp_timeshift);
	// restore hard interrupts
	local_irq_restore(flags);
 
   return IRQ_HANDLED;
}

/***************************************************************************
 * Main timer function
*/
static enum hrtimer_restart timer_Function (struct hrtimer * unused)
{
	int time_delay_nsec = ONE_SECOND;
	int timer_current_time;
	//
	get_Current_Time();
	timer_current_time = sys_time_now_nsec;
	if (timer_current_time > HALF_SECOND)
	{
		timer_current_time -= ONE_SECOND;
	}
	if (sys_gpio_out_level == 0)
	{
		time_delay_nsec = spi_snap_time - timer_current_time - gps_ntp_timeshift;
		sys_gpio_out_level = 1;
		gpio_set_value(GPIO_OUT_GPIO, 0);
		
		//printk(KERN_NOTICE "Hi %d:%d\n", sys_time_now_sec, sys_time_now_nsec);
	}
	else
	{
		time_delay_nsec -= sys_time_now_nsec;
		while (time_delay_nsec < HALF_SECOND)
		{
			time_delay_nsec += ONE_SECOND;
		}
		while (time_delay_nsec > ONE_SECOND + HALF_SECOND)
		{
			time_delay_nsec -= ONE_SECOND;
		}
		sys_gpio_out_level = 0;
		gpio_set_value(GPIO_OUT_GPIO, 1);
		//
		if (sys_time_now_sec == ((sys_time_now_sec / 8) * 8))
		{
			user_Space_Request_Processing();
		}
		//
		//printk(KERN_NOTICE "Lo %d:%d\n", sys_time_now_sec, sys_time_now_nsec);
	}
	
	// Configure next wakeup
	kt_periode = ktime_set(0, time_delay_nsec); //seconds,nanoseconds
    hrtimer_forward_now(& htimer, kt_periode);
    return HRTIMER_RESTART;
}

/***************************************************************************
 * Start timer. Default period will be updated inside called function.
*/
static void gps_Timer_Init (void)
{
    hrtimer_init (& htimer, CLOCK_REALTIME, HRTIMER_MODE_REL);
    htimer.function = timer_Function;
    kt_periode = ktime_set(1, 0); //seconds,nanoseconds
    hrtimer_start(& htimer, kt_periode, HRTIMER_MODE_REL);
}

/***************************************************************************
 * Stop timer
*/
static void gps_Timer_Cleanup (void)
{
	hrtimer_cancel(& htimer);
}
 
/***************************************************************************
 * Configure GPIO_IN and GPIO_OUT. GPIO_IN triggered by falling signal.
*/
void gps_Gpio_Config (void)
{
	
   if (gpio_request(GPIO_OUT_GPIO, GPIO_OUT_GPIO_DESC)) {
      printk("GPIO OUT request failure: %s\n", GPIO_OUT_GPIO_DESC);
      return;
   }
  
   if ( (gpio_direction_output(GPIO_OUT_GPIO, 0)) < 0 ) {
      printk("GPIO OUT set direction failure: %s\n", GPIO_OUT_GPIO_DESC);
      return;
   }
   
   if (gpio_request(GPIO_IN_GPIO, GPIO_IN_GPIO_DESC)) {
      printk("GPIO IN request failure: %s\n", GPIO_IN_GPIO_DESC);
      return;
   }
   
   if ( (irq_in_gpio = gpio_to_irq(GPIO_IN_GPIO)) < 0 ) {
      printk("GPIO to IRQ mapping failure %s\n", GPIO_IN_GPIO_DESC);
      return;
   }
 
   printk(KERN_NOTICE "Mapped int %d\n", irq_in_gpio);
 
   if (request_irq(irq_in_gpio,
                   (irq_handler_t ) r_irq_handler,
                   IRQF_TRIGGER_FALLING,
                   GPIO_IN_GPIO_DESC,
                   GPIO_IN_GPIO_DEVICE_DESC)) {
      printk("Irq Request failure\n");
      return;
   }
   
}
 
/***************************************************************************
 * Stop using GPIOs. Free GPIO resources.
*/
void gps_Gpio_Release(void)
{
	free_irq(irq_in_gpio, GPIO_IN_GPIO_DEVICE_DESC);
	gpio_free(GPIO_OUT_GPIO);
	gpio_free(GPIO_IN_GPIO);
}

/***************************************************************************
 * Start module. One and only function runs on start.
*/
int gps_Init(void)
{
	printk(KERN_NOTICE "Starting Real Time Module \n");
	gps_Gpio_Config();
	gps_Timer_Init();
	get_Current_Time ();
	return 0;
}
 
/***************************************************************************
 * Stop module. Release resources.
*/
void gps_Cleanup(void)
{
	gps_Timer_Cleanup();
	gps_Gpio_Release();
	printk(KERN_NOTICE "Stop Real Time Module \n");
}
 
module_init(gps_Init);
module_exit(gps_Cleanup);
 
/***************************************************************************
 * Credentials
*/
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Slava Nasonov");
MODULE_DESCRIPTION("GPS 1pps Emulator");

