#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <pthread.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <linux/input.h>


#define msleep(x) usleep((x)*1000)

extern int errno;

fd_set fds;
int keyboard_fd, mouse_fd, touchpad_fd;

int m_active_flag = 0;
time_t m_last_tick,m_interval_second;
char m_bl_device[64]={0};
int m_brightness_value = 0;
pthread_mutex_t lasttick_mutex,brightness_mutex;
	
typedef struct{
	char device_event[128];
	char device_name[128];
}device_event_name_t;


void OSSemPend(pthread_mutex_t *mutex)
{
	pthread_mutex_lock(mutex);
}

void OSSemPost(pthread_mutex_t *mutex)
{
	pthread_mutex_unlock(mutex);
}

void str_CR_LF_remove(char *param_str)
{
    int size, i;
    
    size = strlen(param_str);

    /* start check from ending char.*/
    for (i = size - 1; i > 0; --i)
    {
        if ('\r' == param_str[i] 
         || '\n' == param_str[i])
        {
            param_str[i] = '\0';
        }
        else
        {
            break;
        }
    }
}

int check_shell_status(int rv)
{
    /* no child pricess execute.*/
    if (-1 == rv)
    {
        return -1;
    }

    /* check child process normal exit.*/
    if (!WIFEXITED(rv))
    {
        return -1;
    }

    /* check shell execute correct.*/
    if (0 != WEXITSTATUS(rv))  
    {
        return -1;
    }  
    
     return 0;  
}

int utils_system_ex(const char *cmd, char *recv, uint16_t max_size)
{
    int ret = -1, rv, sizet = 0;
    FILE *fp = NULL;
	uint32_t cnt = 0;
	
	/* we use pipe to execute this cmd.*/
	fp = popen(cmd, "r");
	if (NULL == fp)
    {
        return -1;
    }
    
    if (NULL != recv)
    {
        do
        {
            sizet = fread(recv, sizeof(char), max_size, fp);
            if (++cnt > 10)
            {
                sizet = 0;
                goto _per_end;
            }
        }
        while(sizet <= 0 && EINTR == errno);
        
        sizet = (sizet >= max_size) ? max_size - 1 : sizet;
        recv[sizet] = '\0';
    }
    
_per_end:
    rv = pclose(fp);
    if (-1 == rv && ECHILD == errno)
    {
        if (sizet > 0)
        {
            ret = sizet;
        }
    }
    else
    {
        ret = check_shell_status(rv);
        if (0 == ret)
        {
            ret = sizet;
        }
    }
    
	return ret;
}

int input_device_cache(device_event_name_t *eventArray)
{
	char cmd[512] = {0};
	char buffer[128];
	int ret = -1;
	int eventNum = 0;
	
	sprintf(cmd, "ls /sys/class/input/ | grep event | wc -l");
	ret = utils_system_ex(cmd, buffer, 16);
	if(ret < 0)
	{
		return ret;
	}
	
	str_CR_LF_remove(buffer);
	//printf("event number:%s\n", buffer);
	eventNum = atoi(buffer);

	for(int i=0; i < eventNum; i++)
	{
		sprintf(cmd, "ls /sys/class/input/ | grep event | awk 'NR==%d'",i+1 );
		ret = utils_system_ex(cmd, buffer, 16);
		str_CR_LF_remove(buffer);
		
		if(strlen(buffer) > 0)
		{
			strcpy(eventArray[i].device_event, buffer);
			
			sprintf(cmd, "cat /sys/class/input/%s/device/name", eventArray[i].device_event);
			ret = utils_system_ex(cmd, buffer, 128);
			str_CR_LF_remove(buffer);
			strcpy(eventArray[i].device_name, buffer);
		}
		
		if(ret < 0)
		{
			return ret;
		}
		//printf("%s : %s\n",eventArray[i].device_event,eventArray[i].device_name);
	}
	
	return ret;
}

int event_search(device_event_name_t *eventArray, int array_size, char *name, char *event)
{
	char *buff;
		
	for(int i = 0 ; i < array_size; i++)
	{
		buff = strstr(eventArray[i].device_name, name);
		if(NULL != buff)
		{
			strcpy(event,eventArray[i].device_event);
			return 1;
		}
	}
	
	return -1;
}

void get_backlight_device()
{
	char cmd[512] = {0};
	int ret = -1;

	sprintf(cmd, "ls /sys/class/backlight/ | awk 'NR==1'" );
	ret = utils_system_ex(cmd, m_bl_device, 64);
	str_CR_LF_remove(m_bl_device);
	
	if(ret < 0)
	{
		memset(m_bl_device,0,sizeof(m_bl_device));
	}
}

void enable_backlight_power(void)
{
	char cmd[256];
	char buffer[16];
		
	sprintf(cmd, "echo %d | sudo tee /sys/class/backlight/%s/bl_power", 0, m_bl_device);
	utils_system_ex(cmd, buffer, 16);
}

void disable_backlight_power(void)
{
	char cmd[256];
	char buffer[16];
		
	sprintf(cmd, "echo %d | sudo tee /sys/class/backlight/%s/bl_power", 1, m_bl_device);
	utils_system_ex(cmd, buffer, 16);
}

int get_backlight_power(void)
{
	char cmd[256];
	char buf[16];
	
	sprintf(cmd, "cat /sys/class/backlight/%s/bl_power",m_bl_device);
	utils_system_ex(cmd, buf, 16);
	str_CR_LF_remove(buf);
	
	return atoi(buf);
}

void set_backlight_brightness(int value)
{
	char cmd[256];
	char buffer[16];
		
	sprintf(cmd, "echo %d | sudo tee /sys/class/backlight/%s/brightness", value, m_bl_device);
	utils_system_ex(cmd, buffer, 16);
	
	enable_backlight_power();
}

int get_backlight_brightness(void)
{
	char cmd[256];
	char buf[16];
	
	sprintf(cmd, "cat /sys/class/backlight/%s/brightness",m_bl_device);
	utils_system_ex(cmd, buf, 16);
	str_CR_LF_remove(buf);
	
	return atoi(buf);
}


int open_devpath(int *pfd, char *path)
{
    int fd;

    fd = open(path, O_RDONLY | O_NONBLOCK);
    
    if (fd <= 0)
    {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return -1;
    }
    else
    {
        *pfd = fd;
        return 0;
    }
}

/*
void reset_fdset()
{
    FD_ZERO(&fds);
    FD_SET(keyboard_fd, &fds);
    FD_SET(mouse_fd, &fds);
    FD_SET(touchpad_fd, &fds);
}
*/

void handle_keyboard_event(const struct input_event *piev)
{
    printf("Keyboard Event - ");

    switch (piev->type)
    {
    // 键盘按键按压或释放事
    case EV_KEY:
        printf("%d %s", piev->code, piev->value ? "Pressed" : "Released");
        break;
    // 键盘按键持续按压不释放会一直产生此事件，且 piev->value 为持续按下的按键编号
    case EV_MSC:
        printf("stay pressed: %d %d", piev->code, piev->value);
        break;
    // 同步事件
    case EV_SYN:
        printf("---------------------------------------------------------");
        break;
    default:
        printf("Unkown Event: %d, %d, %d", piev->type, piev->code, piev->value);
    }

    printf("\n");
	
	OSSemPend(&lasttick_mutex);
	m_last_tick = time(NULL);
	OSSemPost(&lasttick_mutex);
	
	OSSemPend(&brightness_mutex);
	set_backlight_brightness(m_brightness_value);
	m_active_flag = 1;
	OSSemPost(&brightness_mutex);
}

void handle_mouse_event(const struct input_event *piev)
{
    printf("   Mouse Event - ");

    switch (piev->type)
    {
    // 鼠标左键、右键、中键等按键按压或释放
    case EV_KEY:
        switch (piev->code)
        {
        case BTN_LEFT:
            printf("left is %s", piev->value ? "Pressed" : "Released");
            break;
        case BTN_RIGHT:
            printf("right is %s", piev->value ? "Pressed" : "Released");
            break;
        case BTN_MIDDLE:
            printf("middle is %s", piev->value ? "Pressed" : "Released");
            break;
        // 其他按键未列出
        default:
            printf("unkown EV_KEY: %d %d", piev->code, piev->value);
        }

        break;
    // 鼠标移动或滚轮滚动时产生事件
    case EV_REL:
        switch (piev->code)
        {
        // 鼠标左右移动
        case REL_X:
            if (piev->value > 0)
                printf("right %d", piev->value);
            else if (piev->value < 0)
                printf("left %d", -piev->value);

            break;
        // 鼠标上下移动
        case REL_Y:
            if (piev->value > 0)
                printf("down %d", piev->value);
            else if (piev->value < 0)
                printf("up %d", -piev->value);

            break;
        // 鼠标滚轮上下滚动
        case REL_WHEEL:
            if (piev->value == 1)
                printf("scroll up\n");
            else if (piev->value == -1)
                printf("scroll down\n");
            else
                printf("scoll unkown\n");
            
            break;
        default:
            printf("unkown EV_REL: %d %d", piev->code, piev->value);
        }

        break;
    // 鼠标按键按下或释放同时产生此事件，value 值 - 左：589825，右：589826，中：589827，不知是不是 Magic Number
    case EV_MSC:
        printf("stay pressed: %d %d", piev->code, piev->value);
        break;
    // 同步事件
    case EV_SYN:
        printf("---------------------------------------------------------");
        break;
    default:
        printf("Unkown Event: %d, %d, %d", piev->type, piev->code, piev->value);
    }

    printf("\n");
	
	OSSemPend(&lasttick_mutex);
	m_last_tick = time(NULL);
	OSSemPost(&lasttick_mutex);
	
	OSSemPend(&brightness_mutex);
	set_backlight_brightness(m_brightness_value);
	m_active_flag = 1;
	OSSemPost(&brightness_mutex);
}

void handle_touchpad_event(const struct input_event *piev)
{
    printf("Touchpad Event - ");

    switch (piev->type)
    {
    // 触摸板按键及触摸
    case EV_KEY:
        switch (piev->code)
        {
        // 触摸板下面按键触发，可通过用手指甲触发来避免 BTN_TOUCH 事件
        case BTN_LEFT:
            printf("left %s", piev->value ? "Pressed" : "Released");
            break;
        // 只要有手指触碰，就会产生此事件
        case BTN_TOUCH:
            printf("touch %s", piev->value ? "Pressed" : "Release");
            break;
        // 1 指
        case BTN_TOOL_FINGER:
            printf("1 finger %s", piev->value ? "Pressed" : "Release");
            break;
        // 2 指
        case BTN_TOOL_DOUBLETAP:
            printf("2 finger %s", piev->value ? "Pressed" : "Release");
            break;
        // 3 指
        case BTN_TOOL_TRIPLETAP:
            printf("3 finger %s", piev->value ? "Pressed" : "Release");
            break;
        // 4 指
        case BTN_TOOL_QUADTAP:
            printf("4 finger %s", piev->value ? "Pressed" : "Release");
            break;
        // 5 指
        case BTN_TOOL_QUINTTAP:
            printf("5 finger %s", piev->value ? "Pressed" : "Release");
            break;
        default:
            printf("unkown EV_KEY: %d %d", piev->code, piev->value);
        }
        break;
    case EV_ABS:
        switch (piev->code)
        {
        // 多指操作时产生多个事件，每个之后紧跟一对 ABS_MT_POSITION_{X, Y} 事件
        case ABS_MT_SLOT:
            printf("ABS_SLOT: %d", piev->value);
            break;
        // 当前手指 x 向绝对位置
        case ABS_MT_POSITION_X:
            printf("position x: %d", piev->value);
            break;
        // 当前手指 y 向绝对位置
        case ABS_MT_POSITION_Y:
            printf("position y: %d", piev->value);
            break;
        // 当前 x 向绝对位置，指示第一个检测出来的手指
        case ABS_X:
            printf("x: %d", piev->value);
            break;
        // 当前 y 向绝对位置，指示第一个检测出来的手指
        case ABS_Y:
            printf("y: %d", piev->value);
            break;
        // 手指释放时产生此事件，且 value 恒为 -1
        case ABS_MT_TRACKING_ID:
            printf("ABS_MT_TRACKING_ID: %d", piev->value);
            break;
        default:
            printf("unkown EV_ABS: %d %d", piev->code, piev->value);
        }

        break;
    case EV_MSC:
        switch (piev->code)
        {
        // 每次都会产生，为一个时间戳，暂未搞清
        case MSC_TIMESTAMP:
            printf("timestamp: %d", piev->value);
            break;
        default:
            printf("unkown EV_MSC: %d %d", piev->code, piev->value);
        }

        break;
    // 同步事件
    case EV_SYN:
        printf("---------------------------------------------------------");
        break;
    default:
        printf("Unkown Event: %d, %d, %d", piev->type, piev->code, piev->value);
    }
	
    printf("\n");
	
	OSSemPend(&lasttick_mutex);
	m_last_tick = time(NULL);
	OSSemPost(&lasttick_mutex);
	
	OSSemPend(&brightness_mutex);
	set_backlight_brightness(m_brightness_value);
	m_active_flag = 1;
	OSSemPost(&brightness_mutex);
}


void* screen_blanking_task(void* arg)
{
	time_t last_active_tick = 0;
	int current_brightness = 0;
	int current_bl_power = 0;

	while(1)
	{
		OSSemPend(&lasttick_mutex);
		last_active_tick = m_last_tick;
		OSSemPost(&lasttick_mutex);
		
		if((time(NULL) - last_active_tick) > m_interval_second)
		{
			if(m_active_flag > 0)
			{
				OSSemPend(&brightness_mutex);
				m_brightness_value = get_backlight_brightness();
				disable_backlight_power();
				m_active_flag = 0;
				OSSemPost(&brightness_mutex);
			}
			else
			{
				current_brightness = get_backlight_brightness();
				current_bl_power = get_backlight_power();
				
				//人为主动打开背光
				if((current_brightness > 0)	&& (current_bl_power == 0))	
				{
					m_active_flag = 1;					
				}
				
				OSSemPend(&lasttick_mutex);
				m_last_tick = time(NULL);
				OSSemPost(&lasttick_mutex);
			}
		}
		msleep(100);
	}
	
	return NULL;
}

int main(int argc, char const *argv[])
{
    struct input_event iev;
	struct timeval timeout;
	device_event_name_t eventArray[16];
	
	char touchpad_dev_path[64];
	char mouse_dev_path[64];	
	char keyboard_dev_path[64];
	char event_tmp[16];
	pthread_t backlight_thread = 0;
	int current_brightness = 0;
	int ret = 0;
	
	if(argc > 1)
	{
		m_interval_second = atoi(argv[1]);
		if(m_interval_second < 5)
		{
			m_interval_second = 5;
		}
	}
	else
	{
		m_interval_second = 5;
	}
	
	get_backlight_device();
	m_active_flag = 1;
	
	if(strlen(m_bl_device) == 0)
	{
		return -1;
	}
	
	m_brightness_value = get_backlight_brightness();
	current_brightness = m_brightness_value;
	
	if(current_brightness < 15)
	{
		set_backlight_brightness(255);
	}
	
	pthread_mutex_init(&lasttick_mutex, NULL);
	pthread_mutex_init(&brightness_mutex, NULL);
	
	m_last_tick = time(NULL);
	pthread_create(&backlight_thread, NULL, screen_blanking_task, NULL);
	
	while(1)
	{
		msleep(50);
		FD_ZERO(&fds);
		memset(eventArray,0,sizeof(device_event_name_t)*16);

		ret = input_device_cache(eventArray);
		if(ret > 0)
		{
			if(event_search(eventArray,16,"TouchScreen",event_tmp) > 0)
			{
				sprintf(touchpad_dev_path, "/dev/input/%s", event_tmp);
				open_devpath(&touchpad_fd, touchpad_dev_path);
				FD_SET(touchpad_fd, &fds);
			}

			if((event_search(eventArray,16,"Mouse",event_tmp) > 0) || (event_search(eventArray,16,"MOUSE",event_tmp) > 0))
			{
				sprintf(mouse_dev_path, "/dev/input/%s", event_tmp);
				open_devpath(&mouse_fd, mouse_dev_path);
				FD_SET(mouse_fd, &fds);
			}
			
			if(event_search(eventArray,16,"Keyboard",event_tmp) > 0)
			{
				sprintf(keyboard_dev_path, "/dev/input/%s", event_tmp);
				open_devpath(&keyboard_fd, keyboard_dev_path);
				FD_SET(keyboard_fd, &fds);
			}
			
			timeout.tv_sec	= 0;
			timeout.tv_usec = 250000;

			ret = select(6, &fds, NULL, NULL, &timeout);
				
			if(ret > 0)
			{
				if (FD_ISSET(keyboard_fd, &fds))
				{
					if (read(keyboard_fd, &iev, sizeof(iev)) == sizeof(iev))
						handle_keyboard_event(&iev);
				}
				else if (FD_ISSET(mouse_fd, &fds))
				{
					if (read(mouse_fd, &iev, sizeof(iev)) == sizeof(iev))
						handle_mouse_event(&iev);
				}
				else if (FD_ISSET(touchpad_fd, &fds))
				{
					if (read(touchpad_fd, &iev, sizeof(iev)) == sizeof(iev))
						handle_touchpad_event(&iev);
				}
			}
			
			if(keyboard_fd > 0)
			{
				close(keyboard_fd);	
				keyboard_fd = 0;
			}

			if(mouse_fd > 0)
			{
				close(mouse_fd);
				mouse_fd = 0;
			}
			
			if(touchpad_fd > 0)
			{
				close(touchpad_fd);	
				touchpad_fd = 0;
			}
		}
	}

    return 0;
}
