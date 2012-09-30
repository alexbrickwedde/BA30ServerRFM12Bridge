################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../rfm12b_2.c 

OBJS += \
./rfm12b_2.o 

C_DEPS += \
./rfm12b_2.d 


# Each subdirectory must supply rules for building sources it contributes
%.o: ../%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	arm-linux-gnueabihf-gcc -nostdinc -I"C:\opt\gcc-linaro-arm-linux-gnueabihf\arm-linux-gnueabihf\libc\usr\include" -I"C:\opt\gcc-linaro-arm-linux-gnueabihf\arm-linux-gnueabihf\libc\usr\include\arm-linux-gnueabihf" -I"C:\opt\gcc-linaro-arm-linux-gnueabihf\lib\gcc\arm-linux-gnueabihf\4.7.2\include" -I"C:\Users\SonAle\workspaces\workspace42\BA30ServerRFM12Bridge\wiringPi" -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


