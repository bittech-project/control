#ifndef _STO_COMPILER_H_
#define _STO_COMPILER_H_

#define __must_check	__attribute__((warn_unused_result))

#define sto_unlikely(x)	__builtin_expect(!!(x), 0)
#define sto_likely(x)	__builtin_expect(!!(x), 1)

#endif /* _STO_COMPILER_H_ */
