// ============================================================================/
// ======================= ParU.h =============================================/
// ============================================================================/

// ParU, Copyright (c) 2022-2024, Mohsen Aznaveh and Timothy A. Davis,
// All Rights Reserved.
// SPDX-License-Identifier: GPL-3.0-or-later

//------------------------------------------------------------------------------

// This is the ParU.h file. All user callable routines are in this file and
// all of them start with ParU_*. This file must be included in all user code
// that use ParU.
//
// ParU is a parallel sparse direct solver. This package uses OpenMP tasking
// for parallelism. ParU calls UMFPACK for symbolic analysis phase, after that
// some symbolic analysis is done by ParU itself and then numeric phase
// starts. The numeric computation is a task parallel phase using OpenMP and
// each task calls parallel BLAS; i.e. nested parallism.
//
// The performance of BLAS has a heavy impact on the performance of ParU.
// However, depending on the input problem performance of parallelism in BLAS
// sometimes does not have an effect on the ParU performance.
//
// General Usage for solving Ax = b, where A is a sparse matrix in a CHOLMOD
// sparse matrix data structure with double entries and b is a dense vector of
// double (or a dense matrix B for multiple rhs):
//
//      info = ParU_Analyze(A, &Sym, &Control);
//      info = ParU_Factorize(A, Sym, &Num, &Control);
//      info = ParU_Solve(Sym, Num, b, x, &Control);
//
// See paru_demo for more examples

#ifndef PARU_H
#define PARU_H

// ============================================================================/
// include files and ParU version
// ============================================================================/

#include "SuiteSparse_config.h"
#include "cholmod.h"
#include "umfpack.h"

typedef enum ParU_Info
{
    PARU_SUCCESS = 0,           // everying is fine
    PARU_OUT_OF_MEMORY = -1,    // ParU ran out of memory
    PARU_INVALID = -2,          // inputs are invalid (NULL, for example)
    PARU_SINGULAR = -3,         // matrix is numerically singular
    PARU_TOO_LARGE = -4         // problem too large for the BLAS
} ParU_Info ;

#define PARU_MEM_CHUNK (1024*1024)

#define PARU_DATE "Apr XX, 2024"
#define PARU_VERSION_MAJOR  1
#define PARU_VERSION_MINOR  0
#define PARU_VERSION_UPDATE 0

#define PARU__VERSION SUITESPARSE__VERCODE(1,0,0)
#if !defined (SUITESPARSE__VERSION) || \
    (SUITESPARSE__VERSION < SUITESPARSE__VERCODE(7,8,0))
#error "ParU 1.0.0 requires SuiteSparse_config 7.8.0 or later"
#endif

#if !defined (UMFPACK__VERSION) || \
    (UMFPACK__VERSION < SUITESPARSE__VERCODE(6,3,3))
#error "ParU 1.0.0 requires UMFPACK 6.3.3 or later"
#endif

#if !defined (CHOLMOD__VERSION) || \
    (CHOLMOD__VERSION < SUITESPARSE__VERCODE(5,3,0))
#error "ParU 1.0.0 requires CHOLMOD 5.3.0 or later"
#endif

//  the same values as UMFPACK_STRATEGY defined in UMFPACK/Include/umfpack.h
#define PARU_STRATEGY_AUTO 0         // decided to use sym. or unsym. strategy
#define PARU_STRATEGY_UNSYMMETRIC 1  // COLAMD(A), metis, ...
#define PARU_STRATEGY_SYMMETRIC 3    // prefer diagonal

#if 0
#define UMFPACK_STRATEGY_AUTO 0         /* use sym. or unsym. strategy */
#define UMFPACK_STRATEGY_UNSYMMETRIC 1  /* COLAMD(A), coletree postorder,
                                           not prefer diag*/
#define UMFPACK_STRATEGY_OBSOLETE 2     /* 2-by-2 is no longer available */
#define UMFPACK_STRATEGY_SYMMETRIC 3    /* AMD(A+A'), no coletree postorder,
                                           prefer diagonal */
#endif

// enum for ParU_Get:
typedef enum
{
    // int64_t scalars:
    PARU_GET_N = 0,                 // # of rows/columns of A and its factors
    PARU_GET_ANZ = 1,               // # of entries in input matrix
    PARU_GET_LNZ = 2,               // # of entries in L
    PARU_GET_UNZ = 3,               // # of entries in U
    PARU_GET_NROW_SINGLETONS = 4,   // # of row singletons
    PARU_GET_NCOL_SINGLETONS = 5,   // # of column singletons
    PARU_GET_PARU_STRATEGY = 6,     // strategy used by ParU
    PARU_GET_UMFPACK_STRATEGY = 7,  // strategy used by UMFPACK
    PARU_GET_UMFPACK_ORDERING = 8,  // ordering used by UMFPACK

    // int64_t arrays of size n:
    PARU_GET_P = 101,               // partial pivoting row ordering
    PARU_GET_Q = 102,               // fill-reducing column ordering

    // double scalars:
    PARU_GET_FLOP_COUNT = 201,      // flop count for factorization
    PARU_GET_RCOND_ESTIMATE = 202,  // rcond estimate
    PARU_GET_MIN_UDIAG = 203,       // min (abs (diag (U)))
    PARU_GET_MAX_UDIAG = 204,       // max (abs (diag (U)))

    // double array of size n:
    PARU_GET_ROW_SCALE_FACTORS = 301,   // row scaling factors

    // pointer to const string (const char **):
    PARU_GET_BLAS_LIBRARY_NAME = 401,   // BLAS library used
    PARU_GET_FRONT_TREE_TASKING = 402,  // frontal tree: parallel or sequential
}
ParU_Get_enum ;

// =============================================================================
// ParU C++ definitions ========================================================
// =============================================================================

#ifdef __cplusplus

// The following definitions are only available from C++:

// silence these diagnostics:
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wc++11-extensions"
#endif

#include <cmath>

typedef struct ParU_Symbolic_struct *ParU_Symbolic ;
typedef struct ParU_Numeric_struct  *ParU_Numeric ;

// =============================================================================
// =========================== ParU_Control ====================================
// =============================================================================
// The default value of some control options can be found here. All user
// callable functions use ParU_Control; some controls are used only in symbolic
// phase and some controls are only used in numeric phase.

struct ParU_Control     // FIXME: make opaque
{
    // For all phases of ParU:
    int64_t mem_chunk = PARU_MEM_CHUNK ;  // chunk size for memset and memcpy

    // Numeric factorization parameters:
    double piv_toler = 0.1 ;    // tolerance for accepting sparse pivots
    double diag_toler = 0.001 ; // tolerance for accepting symmetric pivots
    int32_t panel_width = 32 ;  // width of panel for dense factorizaiton
    int32_t trivial = 4 ;       // dgemms smaller than this do not call BLAS
    int32_t worthwhile_dgemm = 512 ; // dgemms bigger than this are tasked
    int32_t worthwhile_trsm = 4096 ; // trsm bigger than this are tasked
    int32_t prescale = 1 ;  // 0: no scaling, 1: scale each row by the max
        // absolute value in its row.

    // Symbolic analysis parameters:
    int32_t umfpack_ordering = UMFPACK_ORDERING_METIS_GUARD ;
    int32_t umfpack_strategy = UMFPACK_STRATEGY_AUTO ;
    int32_t relaxed_amalgamation = 32 ;  // symbolic analysis tries to ensure
        // that each front have more pivot columns than this threshold
    int32_t paru_strategy = PARU_STRATEGY_AUTO ;
    int32_t filter_singletons = 1 ; // filter singletons if nonzero

    // For all phases of ParU:
    int32_t paru_max_threads = 0;   // initialized with omp_max_threads
} ;

//------------------------------------------------------------------------------
// ParU_Version:
//------------------------------------------------------------------------------

// return the version and date of the ParU library.

ParU_Info ParU_Version (int ver [3], char date [128]) ;

//------------------------------------------------------------------------------
// ParU_Analyze: Symbolic analysis is done in this routine. UMFPACK is called
// here and after that some more specialized symbolic computation is done for
// ParU. ParU_Analyze can be called once and can be used for different
// ParU_Factorize calls.
//------------------------------------------------------------------------------

ParU_Info ParU_Analyze
(
    // input:
    cholmod_sparse *A,  // input matrix to analyze of size n-by-n
    // output:
    ParU_Symbolic *Sym_handle,  // output, symbolic analysis
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
// ParU_Factorize: Numeric factorization is done in this routine. Scaling and
// making Sx matrix, computing factors and permutations is here. ParU_Symbolic
// structure is computed ParU_Analyze and is an input in this routine.
//------------------------------------------------------------------------------

ParU_Info ParU_Factorize
(
    // input:
    cholmod_sparse *A,  // input matrix to factorize
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    // output:
    ParU_Numeric *Num_handle,
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
//--------------------- Solve routines -----------------------------------------
//------------------------------------------------------------------------------

// In all the solve routines Num structure must come with the same Sym struct
// that comes from ParU_Factorize

// The vectors x and b have length n, where the matrix factorized is n-by-n.
// The matrices X and B have size n-by-?  nrhs, and are held in column-major
// storage.

//-------- x = A\x -------------------------------------------------------------
ParU_Info ParU_Solve        // solve Ax=b, overwriting b with the solution x
(
    // input:
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    // input/output:
    double *x,              // vector of size n-by-1; right-hand on input,
                            // solution on output
    // control:
    ParU_Control *Control
) ;

//-------- x = A\b -------------------------------------------------------------
ParU_Info ParU_Solve        // solve Ax=b
(
    // input:
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    double *b,              // vector of size n-by-1
    // output
    double *x,              // vector of size n-by-1
    // control:
    ParU_Control *Control
) ;

//-------- X = A\X -------------------------------------------------------------
ParU_Info ParU_Solve        // solve AX=B, overwriting B with the solution X
(
    // input
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    int64_t nrhs,           // # of right-hand sides
    // input/output:
    double *X,              // X is n-by-nrhs, where A is n-by-n;
                            // holds B on input, solution X on input
    // control:
    ParU_Control *Control
) ;

//-------- X = A\B -------------------------------------------------------------
ParU_Info ParU_Solve        // solve AX=B
(
    // input
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    int64_t nrhs,           // # of right-hand sides
    double *B,              // n-by-nrhs, in column-major storage
    // output:
    double *X,              // n-by-nrhs, in column-major storage
    // control:
    ParU_Control *Control
) ;

// Solve L*x=b where x and b are vectors (no scaling or permutations)
ParU_Info ParU_LSolve
(
    // input
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    // input/output:
    double *x,              // n-by-1, in column-major storage;
                            // holds b on input, solution x on input
    // control:
    ParU_Control *Control
) ;

// Solve L*X=B where X and B are matrices (no scaling or permutations)
ParU_Info ParU_LSolve
(
    // input
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    int64_t nrhs,           // # of right-hand-sides (# columns of X)
    // input/output:
    double *X,              // X is n-by-nrhs, where A is n-by-n;
                            // holds B on input, solution X on input
    // control:
    ParU_Control *Control
) ;

// Solve U*x=b where x and b are vectors (no scaling or permutations)
ParU_Info ParU_USolve
(
    // input
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    // input/output
    double *x,              // n-by-1, in column-major storage;
                            // holds b on input, solution x on input
    // control:
    ParU_Control *Control
) ;

// Solve U*X=B where X and B are matrices (no scaling or permutations)
ParU_Info ParU_USolve
(
    // input
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    int64_t nrhs,           // # of right-hand-sides (# columns of X)
    // input/output:
    double *X,              // X is n-by-nrhs, where A is n-by-n;
                            // holds B on input, solution X on input
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
// permutation and inverse permutation, with optional scaling
//------------------------------------------------------------------------------

// apply inverse perm x(p) = b, or with scaling: x(p)=b ; x=x./s
ParU_Info ParU_InvPerm
(
    // inputs
    const int64_t *P,   // permutation vector of size n
    const double *s,    // vector of size n (optional)
    const double *b,    // vector of size n
    int64_t n,          // length of P, s, B, and X
    // output
    double *x,          // vector of size n
    // control:
    ParU_Control *Control
) ;

// apply inverse perm X(p,:) = B or with scaling: X(p,:)=B ; X = X./s
ParU_Info ParU_InvPerm
(
    // inputs
    const int64_t *P,   // permutation vector of size nrows
    const double *s,    // vector of size nrows (optional)
    const double *B,    // array of size nrows-by-ncols
    int64_t nrows,      // # of rows of X and B
    int64_t ncols,      // # of columns of X and B
    // output
    double *X,          // array of size nrows-by-ncols
    // control:
    ParU_Control *Control
) ;

// apply perm and scale x = b(P) / s
ParU_Info ParU_Perm
(
    // inputs
    const int64_t *P,   // permutation vector of size n
    const double *s,    // vector of size n (optional)
    const double *b,    // vector of size n
    int64_t n,          // length of P, s, B, and X
    // output
    double *x,          // vector of size n
    // control:
    ParU_Control *Control
) ;

// apply perm and scale X = B(P,:) / s
ParU_Info ParU_Perm
(
    // inputs
    const int64_t *P,   // permutation vector of size nrows
    const double *s,    // vector of size nrows (optional)
    const double *B,    // array of size nrows-by-ncols
    int64_t nrows,      // # of rows of X and B
    int64_t ncols,      // # of columns of X and B
    // output
    double *X,          // array of size nrows-by-ncols
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
//-------------- computing residual --------------------------------------------
//------------------------------------------------------------------------------

// The user provide both x and b
// resid = norm1(b-A*x) / (norm1(A) * norm1 (x))
ParU_Info ParU_Residual
(
    // inputs:
    cholmod_sparse *A,  // an n-by-n sparse matrix
    double *x,          // vector of size n
    double *b,          // vector of size n
    // output:
    double &resid,      // residual: norm1(b-A*x) / (norm1(A) * norm1 (x))
    double &anorm,      // 1-norm of A
    double &xnorm,      // 1-norm of x
    // control:
    ParU_Control *Control
) ;

// resid = norm1(B-A*X) / (norm1(A) * norm1 (X))
// (multiple rhs)
ParU_Info ParU_Residual
(
    // inputs:
    cholmod_sparse *A,  // an n-by-n sparse matrix
    double *X,          // array of size n-by-nrhs
    double *B,          // array of size n-by-nrhs
    int64_t nrhs,
    // output:
    double &resid,      // residual: norm1(B-A*X) / (norm1(A) * norm1 (X))
    double &anorm,      // 1-norm of A
    double &xnorm,      // 1-norm of X
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
//------------ Get statistics and contents of factorization --------------------
//------------------------------------------------------------------------------

ParU_Info ParU_Get
(
    // input:
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,     // numeric factorization from ParU_Factorize
    ParU_Get_enum field,        // field to get
    // output:
    int64_t *result,            // int64_t result: a scalar or an array
    // control:
    ParU_Control *Control
) ;

ParU_Info ParU_Get
(
    // input:
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    ParU_Get_enum field,        // field to get
    // output:
    double *result,             // double result: a scalar or an array
    // control:
    ParU_Control *Control
) ;

ParU_Info ParU_Get
(
    // input:
    const ParU_Symbolic Sym,    // symbolic analysis from ParU_Analyze
    const ParU_Numeric Num,      // numeric factorization from ParU_Factorize
    ParU_Get_enum field,        // field to get
    // output:
    const char **result,        // string result
    // control:
    ParU_Control *Control
) ;

//------------------------------------------------------------------------------
//------------ Free routines----------------------------------------------------
//------------------------------------------------------------------------------

ParU_Info ParU_FreeNumeric
(
    // input/output:
    ParU_Numeric *Num_handle,  // numeric object to free
    // control:
    ParU_Control *Control
) ;

ParU_Info ParU_FreeSymbolic
(
    // input/output:
    ParU_Symbolic *Sym_handle, // symbolic object to free
    // control:
    ParU_Control *Control
) ;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#endif

// =============================================================================
// ParU C definitions ==========================================================
// =============================================================================

// The following definitions are available in both C and C++:

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

// =============================================================================
// ========================= ParU_C_Control ====================================
// =============================================================================

// FIXME: just use one control struct, not two.  Make it opaque.

// Just like ParU_Control in the C++ interface.  The only difference is the
// initialization which is handled in the C interface, ParU_C_Init_Control.

typedef struct ParU_C_Control_struct        // FIXME: make opaque
{
    // For all phases of ParU:
    int64_t mem_chunk ;                   // chunk size for memset and memcpy

    // Numeric factorization parameters:
    double piv_toler ;          // tolerance for accepting sparse pivots
    double diag_toler ;         // tolerance for accepting symmetric pivots
    int32_t panel_width ;       // width of panel for dense factorizaiton
    int32_t trivial ;           // dgemms smaller than this do not call BLAS
    int32_t worthwhile_dgemm ;       // dgemms bigger than this are tasked
    int32_t worthwhile_trsm ;        // trsm bigger than this are tasked
    int32_t prescale ;      // 0: no scaling, 1: scale each row by the max
        // absolute value in its row.

    // Symbolic analysis parameters:
    int32_t umfpack_ordering ;
    int32_t umfpack_strategy ;
    int32_t relaxed_amalgamation ;       // symbolic analysis tries to ensure
        // that each front have more pivot columns than this threshold
    int32_t paru_strategy ;
    int32_t filter_singletons ;     // filter singletons if nonzero

    // For all phases of ParU:
    int32_t paru_max_threads ;      // initialized with omp_max_threads
} ParU_C_Control ;

typedef struct ParU_C_Symbolic_struct *ParU_C_Symbolic ;
typedef struct ParU_C_Numeric_struct  *ParU_C_Numeric ;

//------------------------------------------------------------------------------
// ParU_Version: return the version and date of ParU
//------------------------------------------------------------------------------

ParU_Info ParU_C_Version (int ver [3], char date [128]) ;

//------------------------------------------------------------------------------
// ParU_C_Init_Control: initialize C data structure
//------------------------------------------------------------------------------

ParU_Info ParU_C_Init_Control (ParU_C_Control *Control_C) ;

//------------------------------------------------------------------------------
// ParU_C_Analyze: Symbolic analysis is done in this routine. UMFPACK is called
// here and after that some more speciaized symbolic computation is done for
// ParU. ParU_Analyze can be called once and can be used for different
// ParU_Factorize calls.
//------------------------------------------------------------------------------

ParU_Info ParU_C_Analyze
(
    // input:
    cholmod_sparse *A,  // input matrix to analyze of size n-by-n
    // output:
    ParU_C_Symbolic *Sym_handle_C,  // output, symbolic analysis
    // control:
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
// ParU_C_Factorize: Numeric factorization is done in this routine. Scaling and
// making Sx matrix, computing factors and permutations is here.
// ParU_C_Symbolic structure is computed ParU_Analyze and is an input in this
// routine.
//------------------------------------------------------------------------------

ParU_Info ParU_C_Factorize
(
    // input:
    cholmod_sparse *A,              // input matrix to factorize of size n-by-n
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_Analyze
    // output:
    ParU_C_Numeric *Num_handle_C,   // output numerical factorization
    // control:
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
//--------------------- Solve routines -----------------------------------------
//------------------------------------------------------------------------------

// In all the solve routines Num structure must come with the same Sym struct
// that comes from ParU_Factorize

// x = A\x, where right-hand side is overwritten with the solution x.
ParU_Info ParU_C_Solve_Axx
(
    // input:
    const ParU_C_Symbolic Sym_C,  // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    // input/output:
    double *x,              // vector of size n-by-1; right-hand on input,
                            // solution on output
    // control:
    ParU_C_Control *Control_C
) ;

// x = L\x, where right-hand side is overwritten with the solution x.
ParU_Info ParU_C_Solve_Lxx
(
    // input:
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    // input/output:
    double *x,              // vector of size n-by-1; right-hand on input,
                            // solution on output
    // control:
    ParU_C_Control *Control_C
) ;

// x = U\x, where right-hand side is overwritten with the solution x.
ParU_Info ParU_C_Solve_Uxx
(
    // input:
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    // input/output:
    double *x,              // vector of size n-by-1; right-hand on input,
                            // solution on output
    // control:
    ParU_C_Control *Control_C
) ;

// x = A\b, for vectors x and b
ParU_Info ParU_C_Solve_Axb
(
    // input:
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    double *b,              // vector of size n-by-1
    // output
    double *x,              // vector of size n-by-1
    // control:
    ParU_C_Control *Control_C
) ;

// X = A\X, where right-hand side is overwritten with the solution X.
ParU_Info ParU_C_Solve_AXX
(
    // input
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    int64_t nrhs,
    // input/output:
    double *X,              // array of size n-by-nrhs in column-major storage,
                            // right-hand-side on input, solution on output.
    // control:
    ParU_C_Control *Control_C
) ;

// X = L\X, where right-hand side is overwritten with the solution X.
ParU_Info ParU_C_Solve_LXX
(
    // input
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    int64_t nrhs,
    // input/output:
    double *X,              // array of size n-by-nrhs in column-major storage,
                            // right-hand-side on input, solution on output.
    // control:
    ParU_C_Control *Control_C
) ;

// X = U\X, where right-hand side is overwritten with the solution X.
ParU_Info ParU_C_Solve_UXX
(
    // input
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    int64_t nrhs,
    // input/output:
    double *X,              // array of size n-by-nrhs in column-major storage,
                            // right-hand-side on input, solution on output.
    // control:
    ParU_C_Control *Control_C
) ;

// X = A\B, for matrices X and B
ParU_Info ParU_C_Solve_AXB
(
    // input
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    int64_t nrhs,
    double *B,              // array of size n-by-nrhs in column-major storage
    // output:
    double *X,              // array of size n-by-nrhs in column-major storage
    // control:
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
// Perm and InvPerm
//------------------------------------------------------------------------------

// apply permutation to a vector, x=b(p)./s
ParU_Info ParU_C_Perm
(
    // inputs
    const int64_t *P,   // permutation vector of size n
    const double *s,    // vector of size n (optional)
    const double *b,    // vector of size n
    int64_t n,          // length of P, s, B, and X
    // output
    double *x,          // vector of size n
    // control:
    ParU_C_Control *Control_C
) ;

// apply permutation to a matrix, X=B(p,:)./s
ParU_Info ParU_C_Perm_X
(
    // inputs
    const int64_t *P,   // permutation vector of size nrows
    const double *s,    // vector of size nrows (optional)
    const double *B,    // array of size nrows-by-ncols
    int64_t nrows,      // # of rows of X and B
    int64_t ncols,      // # of columns of X and B
    // output
    double *X,          // array of size nrows-by-ncols
    // control:
    ParU_C_Control *Control_C
) ;

// apply inverse permutation to a vector, x(p)=b, then scale x=x./s
ParU_Info ParU_C_InvPerm
(
    // inputs
    const int64_t *P,   // permutation vector of size n
    const double *s,    // vector of size n (optional)
    const double *b,    // vector of size n
    int64_t n,          // length of P, s, B, and X
    // output
    double *x,          // vector of size n
    // control
    ParU_C_Control *Control_C
) ;

// apply inverse permutation to a matrix, X(p,:)=b, then scale X=X./s
ParU_Info ParU_C_InvPerm_X
(
    // inputs
    const int64_t *P,   // permutation vector of size nrows
    const double *s,    // vector of size nrows (optional)
    const double *B,    // array of size nrows-by-ncols
    int64_t nrows,      // # of rows of X and B
    int64_t ncols,      // # of columns of X and B
    // output
    double *X,          // array of size nrows-by-ncols
    // control
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
//-------------- computing residual --------------------------------------------
//------------------------------------------------------------------------------

// resid = norm1(b-A*x) / (norm1(A) * norm1 (x))
ParU_Info ParU_C_Residual_bAx
(
    // inputs:
    cholmod_sparse *A,  // an n-by-n sparse matrix
    double *x,          // vector of size n
    double *b,          // vector of size n
    // output:
    double *residc,     // residual: norm1(b-A*x) / (norm1(A) * norm1 (x))
    double *anormc,     // 1-norm of A
    double *xnormc,     // 1-norm of x
    // control:
    ParU_C_Control *Control_C
) ;

// resid = norm1(B-A*X) / (norm1(A) * norm1 (X))
ParU_Info ParU_C_Residual_BAX
(
    // inputs:
    cholmod_sparse *A,  // an n-by-n sparse matrix
    double *X,          // array of size n-by-nrhs
    double *B,          // array of size n-by-nrhs
    int64_t nrhs,
    // output:
    double *residc,     // residual: norm1(B-A*X) / (norm1(A) * norm1 (X))
    double *anormc,     // 1-norm of A
    double *xnormc,     // 1-norm of X
    // control:
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
//------------ ParU_C_Get_*-----------------------------------------------------
//------------------------------------------------------------------------------

ParU_Info ParU_C_Get_INT64
(
    // input:
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    ParU_Get_enum field,        // field to get
    // output:
    int64_t *result,            // int64_t result: a scalar or an array
    // control:
    ParU_C_Control *Control_C
) ;

ParU_Info ParU_C_Get_FP64
(
    // input:
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    ParU_Get_enum field,        // field to get
    // output:
    double *result,            // double result: a scalar or an array
    // control:
    ParU_C_Control *Control_C
) ;

ParU_Info ParU_C_Get_CONSTCHAR
(
    // input:
    const ParU_C_Symbolic Sym_C,    // symbolic analysis from ParU_C_Analyze
    const ParU_C_Numeric Num_C,   // numeric factorization from ParU_C_Factorize
    ParU_Get_enum field,          // field to get
    // output:
    const char **result,          // string result
    // control:
    ParU_C_Control *Control_C
) ;

//------------------------------------------------------------------------------
//------------ Free routines----------------------------------------------------
//------------------------------------------------------------------------------

ParU_Info ParU_C_FreeNumeric
(
    ParU_C_Numeric *Num_handle_C,    // numeric object to free
    // control:
    ParU_C_Control *Control_C
) ;

ParU_Info ParU_C_FreeSymbolic
(
    ParU_C_Symbolic *Sym_handle_C,   // symbolic object to free
    // control:
    ParU_C_Control *Control_C
) ;

#ifdef __cplusplus
}
#endif

#endif

