/*
//@HEADER
// ************************************************************************
//
//               KokkosKernels 0.9: Linear Algebra and Graph Kernels
//                 Copyright 2017 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact Siva Rajamanickam (srajama@sandia.gov)
//
// ************************************************************************
//@HEADER
*/

#ifndef KOKKOS_SPARSE_MV_HPP_
#define KOKKOS_SPARSE_MV_HPP_

#include "Kokkos_Sparse_impl_spmv.hpp"
#include <type_traits>

namespace KokkosSparse {

template<class AlphaType, class AMatrix, class XVector, class BetaType, class YVector>
void
spmv (const char mode[],
      const AlphaType& alpha,
      const AMatrix& A,
      const XVector& x,
      const BetaType& beta,
      const YVector& y,
      const RANK_TWO)
{
  // Make sure that both x and y have the same rank.
  static_assert (XVector::rank == YVector::rank,
                 "KokkosBlas::spmv: Vector ranks do not match.");
  // Make sure that y is non-const.
  static_assert (std::is_same<typename YVector::value_type,
                   typename YVector::non_const_value_type>::value,
                 "KokkosBlas::spmv: Output Vector must be non-const.");

  // Check compatibility of dimensions at run time.
  if ((mode[0] == NoTranspose[0]) || (mode[0] == Conjugate[0])) {
    if ((x.dimension_1 () != y.dimension_1 ()) ||
        (static_cast<size_t> (A.numCols ()) > static_cast<size_t> (x.dimension_0 ())) ||
        (static_cast<size_t> (A.numRows ()) > static_cast<size_t> (y.dimension_0 ()))) {
      std::ostringstream os;
      os << "KokkosBlas::spmv: Dimensions do not match: "
         << ", A: " << A.numRows () << " x " << A.numCols()
         << ", x: " << x.dimension_0 () << " x " << x.dimension_1 ()
         << ", y: " << y.dimension_0 () << " x " << y.dimension_1 ();
      Kokkos::Impl::throw_runtime_exception (os.str ());
    }
  } else {
    if ((x.dimension_1 () != y.dimension_1 ()) ||
        (static_cast<size_t> (A.numCols ()) > static_cast<size_t> (y.dimension_0 ())) ||
        (static_cast<size_t> (A.numRows ()) > static_cast<size_t> (x.dimension_0 ()))) {
      std::ostringstream os;
      os << "KokkosBlas::spmv: Dimensions do not match (transpose): "
         << ", A: " << A.numRows () << " x " << A.numCols()
         << ", x: " << x.dimension_0 () << " x " << x.dimension_1 ()
         << ", y: " << y.dimension_0 () << " x " << y.dimension_1 ();
      Kokkos::Impl::throw_runtime_exception (os.str ());
    }
  }

  typedef KokkosSparse::CrsMatrix<typename AMatrix::const_value_type,
    typename AMatrix::ordinal_type,
    typename AMatrix::device_type,
    Kokkos::MemoryTraits<Kokkos::Unmanaged>,
    typename AMatrix::size_type> AMatrix_Internal;
  AMatrix_Internal A_i = A;

  // Call single-vector version if appropriate
  if (x.dimension_1() == 1) {
    typedef Kokkos::View<typename XVector::const_value_type*,
      typename Kokkos::Impl::if_c<std::is_same<typename YVector::array_layout, Kokkos::LayoutLeft>::value,
                                  Kokkos::LayoutLeft, Kokkos::LayoutStride>::type,
      typename XVector::device_type,
      Kokkos::MemoryTraits<Kokkos::Unmanaged|Kokkos::RandomAccess> > XVector_SubInternal;
    typedef Kokkos::View<typename YVector::non_const_value_type*,
      typename Kokkos::Impl::if_c<std::is_same<typename YVector::array_layout,Kokkos::LayoutLeft>::value,
                                  Kokkos::LayoutLeft,Kokkos::LayoutStride>::type,
      typename YVector::device_type,
      Kokkos::MemoryTraits<Kokkos::Unmanaged> > YVector_SubInternal;

    XVector_SubInternal x_i = Kokkos::subview (x, Kokkos::ALL (), 0);
    YVector_SubInternal y_i = Kokkos::subview (y, Kokkos::ALL (), 0);

    spmv (mode, alpha, A, x_i, beta, y_i);
    return;
  }
  else {
    typedef Kokkos::View<typename XVector::const_value_type**,
      typename XVector::array_layout,
      typename XVector::device_type,
      Kokkos::MemoryTraits<Kokkos::Unmanaged|Kokkos::RandomAccess> > XVector_Internal;
    typedef Kokkos::View<typename YVector::non_const_value_type**,
      typename YVector::array_layout,
      typename YVector::device_type,
      Kokkos::MemoryTraits<Kokkos::Unmanaged> > YVector_Internal;

    XVector_Internal x_i = x;
    YVector_Internal y_i = y;

    return Impl::SPMV_MV<typename AMatrix_Internal::value_type,
                         typename AMatrix_Internal::ordinal_type,
                         typename AMatrix_Internal::device_type,
                         typename AMatrix_Internal::memory_traits,
                         typename AMatrix_Internal::size_type,
                         typename XVector_Internal::value_type**,
                         typename XVector_Internal::array_layout,
                         typename XVector_Internal::device_type,
                         typename XVector_Internal::memory_traits,
                         typename YVector_Internal::value_type**,
                         typename YVector_Internal::array_layout,
                         typename YVector_Internal::device_type,
                         typename YVector_Internal::memory_traits>::spmv_mv (mode, alpha, A, x, beta, y);
  }
}

/// \brief Public interface to local sparse matrix-vector multiply.
///
/// Compute y = beta*y + alpha*Op(A)*x, where x and y are either both
/// rank 1 (single vectors) or rank 2 (multivectors) Kokkos::View
/// instances, A is a KokkosSparse::CrsMatrix, and Op(A) is determined
/// by \c mode.  If beta == 0, ignore and overwrite the initial
/// entries of y; if alpha == 0, ignore the entries of A and x.
///
/// \param mode [in] "N" for no transpose, "T" for transpose, or "C"
///   for conjugate transpose.
/// \param alpha [in] Scalar multiplier for the matrix A.
/// \param A [in] The sparse matrix; KokkosSparse::CrsMatrix instance.
/// \param x [in] Either a single vector (rank-1 Kokkos::View) or
///   multivector (rank-2 Kokkos::View).
/// \param beta [in] Scalar multiplier for the (multi)vector y.
/// \param y [in/out] Either a single vector (rank-1 Kokkos::View) or
///   multivector (rank-2 Kokkos::View).  It must have the same number
///   of columns as x.
template <class AlphaType, class AMatrix, class XVector, class BetaType, class YVector>
void
spmv(const char mode[],
     const AlphaType& alpha,
     const AMatrix& A,
     const XVector& x,
     const BetaType& beta,
     const YVector& y) {
  typedef typename Kokkos::Impl::if_c<XVector::rank == 2, RANK_TWO, RANK_ONE>::type RANK_SPECIALISE;
  spmv (mode, alpha, A, x, beta, y, RANK_SPECIALISE ());
}

} // namespace KokkosSparse

#endif // KOKKOS_SPARSE_MV_HPP_

