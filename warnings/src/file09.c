int f09(int a, int b)
{
    if (a = b) {           /* -Wparentheses */
        return a;
    }
    return b;
}
