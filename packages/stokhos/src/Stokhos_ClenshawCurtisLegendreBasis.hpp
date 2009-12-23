// $Id$ 
// $Source$ 
// @HEADER
// ***********************************************************************
// 
//                           Stokhos Package
//                 Copyright (2009) Sandia Corporation
// 
// Under terms of Contract DE-AC04-94AL85000, there is a non-exclusive
// license for use of this work by or on behalf of the U.S. Government.
// 
// This library is free software; you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as
// published by the Free Software Foundation; either version 2.1 of the
// License, or (at your option) any later version.
//  
// This library is distributed in the hope that it will be useful, but
// WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//  
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
// USA
// Questions? Contact Eric T. Phipps (etphipp@sandia.gov).
// 
// ***********************************************************************
// @HEADER

#ifndef STOKHOS_LEGENDREBASIS_HPP
#define STOKHOS_LEGENDREBASIS_HPP

#include "Stokhos_RecurrenceBasis.hpp"

namespace Stokhos {

  //! Legendre polynomial basis using Clenshaw-Curtis quadrature points
  /*!
   * This is the same as Stokhos::LegendreBasis, but uses Clenshaw-Curtis
   * quadrature points (instead of Gauss-Legendre) for sparse grids only.
   */
  template <typename ordinal_type, typename value_type>
  class ClenshawCurtisLegendreBasis : 
    public RecurrenceBasis<ordinal_type, value_type> {
  public:

    //! Constructor
    /*!
     * \param p order of the basis
     * \param normalize whether polynomials should be given unit norm
     */
    ClenshawCurtisLegendreBasis(ordinal_type p, bool normalize = false);

    //! Destructor
    ~ClenshawCurtisLegendreBasis();

    //! \name Implementation of Stokhos::OneDOrthogPolyBasis methods
    //@{ 

    //! Get sparse grid rule number as defined by Dakota's \c webbur package
    /*!
     * This method is needed for building Smolyak sparse grids out of this 
     * basis.
     */
    virtual ordinal_type getRule() const { return 1; }

    //! Get quadrature weight factor as defined by Dakota's \c webbur package
    /*!
     * This method is needed for building Smolyak sparse grids out of this 
     * basis.
     */
    virtual value_type getQuadWeightFactor() const { return 0.5; }

    //! Get quadrature point factor as defined by Dakota's \c webbur package
    /*!
     * This method is needed for building Smolyak sparse grids out of this 
     * basis.
     */
    virtual value_type getQuadPointFactor() const { return 1.0; }

    //@}

  protected:

    //! \name Implementation of Stokhos::RecurrenceBasis methods
    //@{ 

    //! Compute recurrence coefficients
    virtual void 
    computeRecurrenceCoefficients(ordinal_type n,
				  Teuchos::Array<value_type>& alpha,
				  Teuchos::Array<value_type>& beta,
				  Teuchos::Array<value_type>& delta) const;

    //@}

  private:

    // Prohibit copying
    ClenshawCurtisLegendreBasis(const ClenshawCurtisLegendreBasis&);

    // Prohibit Assignment
    ClenshawCurtisLegendreBasis& operator=(const ClenshawCurtisLegendreBasis& b);

  }; // class ClenshawCurtisLegendreBasis

} // Namespace Stokhos

// Include template definitions
#include "Stokhos_ClenshawCurtisLegendreBasisImp.hpp"

#endif
