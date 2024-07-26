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
#include "mpu6050reg.h"

#define MPU6050_NAME "mpu6050"
#define MPU6050_CNT  1


struct mpu6050_dev{
    dev_t devid;
    int major;
    int minor;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    struct i2c_client *client;
    struct regmap *regmap;
    struct regmap_config regmap_config;

    unsigned char mpu6050_id;
    short int accelX;
    short int accelY;
    short int accelZ;
    short int gyroX;
    short int gyroY;
    short int gyroZ;
    short int temper;

};

static struct mpu6050_dev mpu6050dev;

static void mpu6050_write_reg(struct mpu6050_dev *dev,u8 reg,u8 value)
{
    regmap_write(dev->regmap,reg,value);
}

static unsigned char mpu6050_read_reg(struct mpu6050_dev *dev,u8 reg)
{
    u8 ret;
    unsigned int data;
    ret = regmap_read(dev->regmap,reg,&data);
    return(unsigned char)data;
}

static short int mpu6050_read_two(struct mpu6050_dev *dev,u8 reg)
{
    u8 ret;
    int data[2];
    short int data_ret;
    ret = regmap_read(dev->regmap,reg,&data[0]);
    ret = regmap_read(dev->regmap,reg+1,&data[1]);
    data_ret = (short int)data[0]*256+(short int)data[1];
    return(short int)data_ret;
}

static int mpu6050_reg_init(void)
{
    mpu6050dev.mpu6050_id = mpu6050_read_reg(&mpu6050dev,MPU6050_WHO_AM_I);
    if(mpu6050dev.mpu6050_id!=0x68)
    {
        printk("error id\n");
        return -1;
    }
    printk("mpu6050_id:%x\n",mpu6050dev.mpu6050_id);
    mpu6050_write_reg(&mpu6050dev,MPU6050_PWR_MGMT_1,0x01);
    mpu6050_write_reg(&mpu6050dev,MPU6050_PWR_MGMT_2,0x00);
    mpu6050_write_reg(&mpu6050dev,MPU6050_SMPLRT_DIV,0x09);
    mpu6050_write_reg(&mpu6050dev,MPU6050_CONFIG,0x06);
    mpu6050_write_reg(&mpu6050dev,MPU6050_GYRO_CONFIG,0x18);
    mpu6050_write_reg(&mpu6050dev,MPU6050_ACCEL_CONFIG,0x18);
    return 0;
}

void mpu6050_get(void)
{
    mpu6050dev.accelX = mpu6050_read_two(&mpu6050dev,MPU6050_ACCEL_XOUT_H);
    mpu6050dev.accelY = mpu6050_read_two(&mpu6050dev,MPU6050_ACCEL_YOUT_H);
    mpu6050dev.accelZ = mpu6050_read_two(&mpu6050dev,MPU6050_ACCEL_ZOUT_H);
    mpu6050dev.gyroX = mpu6050_read_two(&mpu6050dev,MPU6050_GYRO_XOUT_H);
    mpu6050dev.gyroY = mpu6050_read_two(&mpu6050dev,MPU6050_GYRO_YOUT_H);
    mpu6050dev.gyroZ = mpu6050_read_two(&mpu6050dev,MPU6050_GYRO_ZOUT_H);
    mpu6050dev.temper = mpu6050_read_two(&mpu6050dev,MPU6050_TEMP_OUT_H);
}



static int mpu6050_open(struct inode *inode,struct file *filp)
{
    filp->private_data = &mpu6050dev;
    mpu6050_reg_init();
    return 0;
}

static ssize_t mpu6050_read(struct file *filp,char __user *buf,size_t cnt,loff_t *off)
{
    struct mpu6050_dev *dev = (struct mpu6050_dev *)filp->private_data;
    short int data[7];
    mpu6050_get();
    data[0] = dev->gyroX;
    data[1] = dev->gyroY;
    data[2] = dev->gyroZ;
    data[3] = dev->accelX;
    data[4] = dev->accelY;
    data[5] = dev->accelZ;
    data[6] = dev->temper;
    copy_to_user(buf,data,sizeof(data));
    return 0;
}

static int mpu6050_release(struct inode *inode ,struct file *filp)
{
    return 0;
}

static const struct file_operations mpu6050_ops = {
    .owner = THIS_MODULE,
    .open = mpu6050_open,
    .read = mpu6050_read,
    .release = mpu6050_release,
};

static int mpu6050_probe(struct i2c_client *client,const struct i2c_device_id *id)
{
    printk("mpu6050_probe\n");

    mpu6050dev.regmap_config.reg_bits = 8;
    mpu6050dev.regmap_config.val_bits = 8;
    mpu6050dev.regmap = regmap_init_i2c(client,&mpu6050dev.regmap_config);

    if(mpu6050dev.major)
    {
        mpu6050dev.minor=0;
        mpu6050dev.devid = MKDEV(mpu6050dev.major,mpu6050dev.minor);
        register_chrdev_region(mpu6050dev.devid,MPU6050_CNT,MPU6050_NAME);
    }
    else
    {
        alloc_chrdev_region(&mpu6050dev.devid,0,MPU6050_CNT,MPU6050_NAME);
        mpu6050dev.major = MAJOR(mpu6050dev.devid);
        mpu6050dev.minor = MINOR(mpu6050dev.devid);
    }

    cdev_init(&mpu6050dev.cdev,&mpu6050_ops);
    cdev_add(&mpu6050dev.cdev,mpu6050dev.devid,MPU6050_CNT);

    mpu6050dev.class = class_create(THIS_MODULE,MPU6050_NAME);
    if(IS_ERR(mpu6050dev.class))
    {
        return PTR_ERR(mpu6050dev.class);
    }

    mpu6050dev.device = device_create(mpu6050dev.class,NULL,mpu6050dev.devid,NULL,MPU6050_NAME);
    if(IS_ERR(mpu6050dev.device))
    {
        return PTR_ERR(mpu6050dev.device);
    }
    mpu6050dev.client = client;
    return 0;
    
}

static int mpu6050_remove(struct i2c_client *client)
{
    
    cdev_del(&mpu6050dev.cdev);
    unregister_chrdev_region(mpu6050dev.devid,MPU6050_CNT);

    device_destroy(mpu6050dev.class,mpu6050dev.devid);
    class_destroy(mpu6050dev.class);
    regmap_exit(mpu6050dev.regmap);
    return 0;
}
 static const struct i2c_device_id ap3216c_id[] = {
    {"myiic,mpu6050", 0},
    {}
 };

static const struct of_device_id mpu6050_of_match[] = {
    {.compatible = "myiic,mpu6050"},
    {}
};



static struct i2c_driver mpu6050_driver = {
    .probe = mpu6050_probe,
    .remove = mpu6050_remove,
    .driver = {
        .owner = THIS_MODULE,
        .name = "mpu6050_dev",
        .of_match_table = mpu6050_of_match,

    },
    .id_table = ap3216c_id,
};


static int __init mpu6050_init(void)
{
    int ret = 0;
    ret = i2c_add_driver(&mpu6050_driver);
    return ret;
}   

static void __exit mpu6050_exit(void)
{
    i2c_del_driver(&mpu6050_driver);
}


module_init(mpu6050_init);
module_exit(mpu6050_exit);
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Norgin");