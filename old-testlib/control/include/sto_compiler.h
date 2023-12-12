#ifndef _STO_COMPILER_H_
#define _STO_COMPILER_H_

#define __must_check	__attribute__((warn_unused_result))

#define STO_BUILD_BUG_ON(cond)	_Static_assert(!(cond), "static assertion failure")

#endif /* _STO_COMPILER_H_ */
