#include "stdio.h"
#include "unistd.h"
#include "sys/types.h"
#include "sys/stat.h"
#include "fcntl.h"
#include "stdlib.h"
#include "string.h"
/***************************************************************
Copyright © ALIENTEK Co., Ltd. 1998-2029. All rights reserved.
文件名		: ledApp.c
作者	  	: 左忠凯
版本	   	: V1.0
描述	   	: chrdevbase驱测试APP。
其他	   	: 无
使用方法	 ：./ledtest /dev/led  0 关闭LED
		     ./ledtest /dev/led  1 打开LED		
论坛 	   	: www.openedv.com
日志	   	: 初版V1.0 2019/1/30 左忠凯创建
***************************************************************/


/*
 * @description		: main主程序
 * @param - argc 	: argv数组元素个数
 * @param - argv 	: 具体参数
 * @return 			: 0 成功;其他 失败
 */

int main(int argc, char *argv[])
{
	int fd, err;
	char *filename;
	short int data[7];
	unsigned short ax,ay,az,gx,gy,gz,temper;

	if(argc !=2){
		printf("error usage!\r\n");
		return -1;
	}
	filename = argv[1];
	fd = open(filename,O_RDWR);
	if(fd<0){
		printf("file %s open failed!\r\n",filename);
		return -1;
	}
	while (1)
	{
		err = read(fd,data,sizeof(data));
		if(err == 0){
			ax = data[3];
			ay = data[4];
			az = data[5];
			gx = data[0];
			gy = data[1];
			gz = data[2];
			temper = data[6];
			printf("mpu6050 ax = %d,ay = %d,az = %d,gx = %d, gy = %d,gz = %d,temper = %.3f\r\n",ax,ay,az,gx,gy,gz,(((float)temper)/340.0f+36.53f));
		}
		usleep(2000000);
	}
	
	close(fd);
	return 0;
}
