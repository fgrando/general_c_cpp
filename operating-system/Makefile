# sources based on tutorial from https://youtu.be/1rnA6wpF0o4

GPPPARAMS = -m32 -fno-use-cxa-atexit -nostdlib -fno-builtin -fno-rtti -fno-exceptions -fno-leading-underscore
ASMPARAMS = --32
LDPARAMS = -melf_i386

objects = loader.o kernel.o

%o: %.cpp
	g++ $(GPPPARAMS) -o $@ -c $<

%o: %.s
	as $(ASMPARAMS) -o $@ $<

mykernel.bin: linker.ld $(objects)
	ld $(LDPARAMS) -T $< -o $@ $(objects)

install: mykernel.bin
	sudo cp $< /boot/mykernel.bin

clean:
	rm *.o *.bin
