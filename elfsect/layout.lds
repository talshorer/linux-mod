SECTIONS
{
    .dummies : {
        __start_dummies = . ;
        *(.dummies)
        __stop_dummies = . ;
    }
}
