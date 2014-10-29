#ifndef _ELFSECT_H
#define _ELFSECT_H

typedef char *(*elfsect_dummy)(void);

#define __elfsect_dummy_symbol \
	__attribute__((__used__)) __attribute__((__section__(".dummies")))

#define elfsect_define_dummy_func(name) \
	static char *name(void) { return #name; } \
	static __elfsect_dummy_symbol elfsect_dummy __dummies__##name = &name;


#endif /* _ELFSECT_H */
