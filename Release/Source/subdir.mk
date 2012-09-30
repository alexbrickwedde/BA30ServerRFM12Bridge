################################################################################
# Automatically-generated file. Do not edit!
################################################################################

# Add inputs and outputs from these tool invocations to the build variables 
C_SRCS += \
../Source/rfm12bridge.c 

OBJS += \
./Source/rfm12bridge.o 

C_DEPS += \
./Source/rfm12bridge.d 


# Each subdirectory must supply rules for building sources it contributes
Source/%.o: ../Source/%.c
	@echo 'Building file: $<'
	@echo 'Invoking: Cross GCC Compiler'
	arm-linux-gnueabihf-gcc -nostdinc -I"C:\opt\gcc-linaro-arm-linux-gnueabihf\arm-linux-gnueabihf\libc\usr\include" -I"C:\opt\gcc-linaro-arm-linux-gnueabihf\arm-linux-gnueabihf\libc\usr\include\arm-linux-gnueabihf" -I"C:\opt\gcc-linaro-arm-linux-gnueabihf\lib\gcc\arm-linux-gnueabihf\4.7.2\include" -I"C:\Users\SonAle\workspaces\workspace42\BA30ServerRFM12Bridge\wiringPi" -O3 -Wall -c -fmessage-length=0 -MMD -MP -MF"$(@:%.o=%.d)" -MT"$(@:%.o=%.d)" -o "$@" "$<"
	@echo 'Finished building: $<'
	@echo ' '


