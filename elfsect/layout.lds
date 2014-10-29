/*
 * special thanks to Ilya Matveychikov on his answer @stackoverflow to
 * http://stackoverflow.com/questions/18673149/using-elf-section-in-lkm
 */
SECTIONS
{
    .dummies : {
        __start_dummies = . ;
        *(.dummies)
        __stop_dummies = . ;
    }
}
