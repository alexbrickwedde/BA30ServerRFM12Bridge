################################################################################
# Automatically-generated file. Do not edit!
################################################################################

-include ../makefile.init

RM := rm -rf

# All of the sources participating in the build are defined here
-include sources.mk
-include Source/subdir.mk
-include subdir.mk
-include objects.mk

ifneq ($(MAKECMDGOALS),clean)
ifneq ($(strip $(C_DEPS)),)
-include $(C_DEPS)
endif
endif

-include ../makefile.defs

# Add inputs and outputs from these tool invocations to the build variables 

# All Target
all: BA30ServerRFM12Bridge

# Tool invocations
BA30ServerRFM12Bridge: $(OBJS) $(USER_OBJS)
	@echo 'Building target: $@'
	@echo 'Invoking: Cross GCC Linker'
	arm-linux-gnueabihf-gcc -L"C:\opt\gcc-linaro-arm-linux-gnueabihf\arm-linux-gnueabihf\libc\usr\lib" -L"C:\Users\SonAle\workspaces\workspace42\BA30ServerRFM12Bridge\wiringPi" -o "BA30ServerRFM12Bridge" $(OBJS) $(USER_OBJS) $(LIBS)
	@echo 'Finished building target: $@'
	@echo ' '

# Other Targets
clean:
	-$(RM) $(OBJS)$(C_DEPS)$(EXECUTABLES) BA30ServerRFM12Bridge
	-@echo ' '

.PHONY: all clean dependents
.SECONDARY:

-include ../makefile.targets
