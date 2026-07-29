int f06(int x)
{
    if (x > 0) {
        return 1;
    }
    /* falls off the end -> -Wreturn-type */
}
