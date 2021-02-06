
void printf(const char* str)
{
    unsigned short* VideoMemory = (unsigned short*)0xb8000;

    for (int i=0; str[i] != '\0'; ++i)
    {
        VideoMemory[i] = (VideoMemory[i] & 0xFF00) | str[i];
    }
}


void kernelMain(void* multiboot_st, unsigned int magicnumber)
{

    printf("hello from the moon!");

    while(1) {}
}