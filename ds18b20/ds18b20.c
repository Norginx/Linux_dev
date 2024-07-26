#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/atomic.h>
#include <linux/string.h>
#include <linux/irq.h>
#include <linux/of_irq.h>
#include <linux/interrupt.h>
#include <asm/mach/map.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/input.h>
#include <linux/i2c.h>
#include <linux/delay.h>
#include <linux/regmap.h>

#define DS18B20_NAME "ds18b20dev1"
#define DS18B20_CNT 1


struct ds18b20_dev{
    dev_t devid;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    int major;
    int minor;
    struct device_node *nd;
    int ds18b20_gpio;
    spinlock_t lock; //自旋锁，由于读ds18b20寄存器的时候，如果被打断有可能导致读写错误，所以加入自旋锁
};

struct ds18b20_dev ds18b20dev;
//封装的函数（参考STM32的代码）
void ds18b20_io_in(void)    //设置为输入
{
    gpio_direction_input(ds18b20dev.ds18b20_gpio);
}
void ds18b20_io_out(bool value)     //设置为输出
{
    if(value)
    {
        gpio_direction_output(ds18b20dev.ds18b20_gpio,1);
    }
    else
    {
        gpio_direction_output(ds18b20dev.ds18b20_gpio,0);
    }
    
}
int ds18b20_io_get(void)
{
    return gpio_get_value(ds18b20dev.ds18b20_gpio);
}

void ds18b20_rst(void)
{
    ds18b20_io_out(0);
    udelay(600);//us级延时用内核定时器无法实现,这里用内核提供的短延时（系统节拍为100hz，实现不了us延时)
    ds18b20_io_out(1);
    udelay(40);
}

uint8_t ds18b20_check(void)//检查有没有ds18b20
{
    uint8_t retry = 0;
    ds18b20_io_in();
    while(ds18b20_io_get()&&retry<200)
    {
        retry++;
        udelay(1);
    }
    if(retry>=200)return 1;
    else retry=0;
    while(!ds18b20_io_get()&&retry<240)
    {
        retry++;
        udelay(1);
    }
    if(retry>=240)return 1;
    return 0;//存在
}



uint8_t da18b20_readbyte(void)//读一个字节
{
    uint8_t data =0;
	uint8_t i=0;
	for(i=0;i<8;i++)
	{
		data=data>>1;
		ds18b20_io_out(0);   //输出状态
		udelay(2);
		ds18b20_io_out(1);
		udelay(2);
		ds18b20_io_in();   //输入状态
		if(ds18b20_io_get())
			data|=0x80;     //放入高位，再移位到低位
		udelay(60);
	}
	return data;
}

void ds18b20_writebyte(uint8_t dat)//写一字节 
{
    uint8_t j=0;
    ds18b20_io_out(1);
    for(j=0;j<8;j++)
    {
        ds18b20_io_out(0);  //输出状态
		udelay(2);
		if(dat&0x01)     //低位开始
		{
			ds18b20_io_out(1);
		}
		else
		{
			ds18b20_io_out(0);
		}
		udelay(60);
		ds18b20_io_out(1);
		dat = dat>>1;
        
    }
}

uint8_t ds18b20_dev_init(void)//硬件初始
{
    ds18b20_rst();
    return ds18b20_check();
}

void ds18b20_start(void)//开始温度转换
{
    ds18b20_rst();
    ds18b20_check();
    ds18b20_writebyte(0xcc);
    ds18b20_writebyte(0x44);
}

uint16_t ds18b20_gettemp(void)//获取一次温度
{
    unsigned long flags;
    uint8_t temperH,temperL;
    uint16_t buffer;
    spin_lock_irqsave(&ds18b20dev.lock,flags);//获取锁，并禁用中
    ds18b20_start();
    ds18b20_rst();
    spin_unlock_irqrestore(&ds18b20dev.lock,flags);//解除锁
    msleep(600);
    spin_lock_irqsave(&ds18b20dev.lock,flags);
    ds18b20_writebyte(0xcc);
    ds18b20_writebyte(0xbe);
    temperL=da18b20_readbyte();
    temperH=da18b20_readbyte();
    spin_unlock_irqrestore(&ds18b20dev.lock,flags);//解除锁
    buffer = temperH*256+temperL;
    return buffer;
}


static int ds18b20_open(struct inode *inode,struct file *filp)
{
    //这里写DS18B20的初始化
    uint8_t ret = ds18b20_dev_init();
    if(ret)
    {
        printk("init failed!\r\n");
    }
    else
    {
        printk("init success!\r\n");
    }
    
    return 0;
}

static ssize_t ds18b20_read(struct file *filp,char __user *buf,size_t cnt,loff_t *offt)
{
    //这里写DS18B20的读函数
    int ret;
    int temper;
    temper=ds18b20_gettemp();
    ret = copy_to_user(buf,&temper,sizeof(temper));
    return ret;
}

static int ds18b20_release(struct inode *inode,struct file *filp)
{
    //如果有必要可以在这写去初始化，但这里没用到
    return 0;
}


static struct file_operations ds18b20_fops = {
    .owner = THIS_MODULE,
    .open = ds18b20_open,
    .read = ds18b20_read,
    .release = ds18b20_release,
};


static int __init ds18b20_init(void)
{
    int ret;
    ds18b20dev.nd = of_find_node_by_path("/ds18b20");
    //获取设备节点
    if(ds18b20dev.nd==NULL)
    {
        printk("Cant found the nd\r\n");
        return -EINVAL;
    }
    //获取设备节点下的ds18b20-gpio属性值
    ds18b20dev.ds18b20_gpio = of_get_named_gpio(ds18b20dev.nd,"ds18b20-gpio",0);
    if(ds18b20dev.ds18b20_gpio<0)
    {
        printk("Cant found the ds18b20-gpio\r\n");
    }
    //先请求IO第一个参数是请求的io标号，第二个是取的名字
    gpio_request(ds18b20dev.ds18b20_gpio,"dsgpio");
    //设置gpio的初始状态,输出模式并输出高电平
    ret = gpio_direction_output(ds18b20dev.ds18b20_gpio,1);//io的标号，输出值
    //为了便于写读写函数对DS18B20寄存器读写并上报，这里用一个字符设备驱动框架
    if(ds18b20dev.major)//指定了设备号
    {
        ds18b20dev.minor=0;
        ds18b20dev.devid = MKDEV(ds18b20dev.major,ds18b20dev.minor);
        register_chrdev_region(ds18b20dev.devid,DS18B20_CNT,DS18B20_NAME);
    }
    else
    {
        alloc_chrdev_region(&ds18b20dev.devid,0,DS18B20_CNT,DS18B20_NAME);
        ds18b20dev.major = MAJOR(ds18b20dev.devid);
        ds18b20dev.minor = MINOR(ds18b20dev.devid);
    }
    printk("ds18b20 major = %d,minor = %d\r\n",ds18b20dev.major,ds18b20dev.minor);
    //注册字符操作设备
    ds18b20dev.cdev.owner = THIS_MODULE;
    cdev_init(&ds18b20dev.cdev,&ds18b20_fops);
    cdev_add(&ds18b20dev.cdev,ds18b20dev.devid,DS18B20_CNT);
    //创建一个设备类
    ds18b20dev.class = class_create(THIS_MODULE,DS18B20_NAME);
    if(IS_ERR(ds18b20dev.class))
    {
        printk("error create class\r\n");
        return PTR_ERR(ds18b20dev.class);
    }
    //创建一个设备
    ds18b20dev.device = device_create(ds18b20dev.class,NULL,ds18b20dev.devid,NULL,DS18B20_NAME);
    if(IS_ERR(ds18b20dev.device))
    {
        printk("create device error\r\n");
        return PTR_ERR(ds18b20dev.device);
    }
    //初始化自旋锁
    spin_lock_init(&ds18b20dev.lock);
    return 0;
    
}   

static void __exit ds18b20_exit(void)
{
    gpio_free(ds18b20dev.ds18b20_gpio);
    cdev_del(&ds18b20dev.cdev);
    unregister_chrdev_region(ds18b20dev.devid,DS18B20_CNT);

    device_destroy(ds18b20dev.class,ds18b20dev.devid);
    class_destroy(ds18b20dev.class);
}


module_init(ds18b20_init);
module_exit(ds18b20_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Norgin");