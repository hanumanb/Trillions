// @HEADER
//
// ***********************************************************************
//
//        MueLu: A package for multigrid based preconditioning
//                  Copyright 2012 Sandia Corporation
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
// Questions? Contact
//                    Jonathan Hu       (jhu@sandia.gov)
//                    Andrey Prokopenko (aprokop@sandia.gov)
//                    Ray Tuminaro      (rstumin@sandia.gov)
//
// ***********************************************************************
//
// @HEADER
#ifndef MUELU_RAPFACTORY_DEF_HPP
#define MUELU_RAPFACTORY_DEF_HPP

// disable clang warnings
#ifdef __clang__
#pragma clang system_header
#endif


#include <sstream>

#include <Xpetra_Matrix.hpp>
#include <Xpetra_MatrixFactory.hpp>
#include <Xpetra_Vector.hpp>
#include <Xpetra_VectorFactory.hpp>

#include "MueLu_RAPFactory_decl.hpp"
#include "MueLu_Utilities.hpp"
#include "MueLu_Monitor.hpp"

namespace MueLu {

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node, class LocalMatOps>
  RAPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node, LocalMatOps>::RAPFactory()
    : implicitTranspose_(false), checkAc_(false), repairZeroDiagonals_(false), hasDeclaredInput_(false) { }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node, class LocalMatOps>
  RCP<const ParameterList> RAPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node, LocalMatOps>::GetValidParameterList(const ParameterList& paramList) const {
    RCP<ParameterList> validParamList = rcp(new ParameterList());

    validParamList->set< RCP<const FactoryBase> >("A",                Teuchos::null, "Generating factory of the matrix A used during the prolongator smoothing process");
    validParamList->set< RCP<const FactoryBase> >("P",                Teuchos::null, "Prolongator factory");
    validParamList->set< RCP<const FactoryBase> >("R",                Teuchos::null, "Restrictor factory");
    validParamList->set< RCP<const FactoryBase> >("AP Pattern",       Teuchos::null, "AP pattern factory");
    validParamList->set< RCP<const FactoryBase> >("RAP Pattern",      Teuchos::null, "RAP pattern factory");
    validParamList->set< bool >(                  "Keep AP Pattern",  false,         "Keep the AP pattern (for reuse)");
    validParamList->set< bool >(                  "Keep RAP Pattern", false,         "Keep the RAP pattern (for reuse)");

    validParamList->set< bool >(                  "CheckMainDiagonal", false,        "Check main diagonal for zeros (default = false).");
    validParamList->set< bool >(                  "RepairMainDiagonal", false,       "Repair zeros on main diagonal (default = false).");

    return validParamList;
  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node, class LocalMatOps>
  void RAPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node, LocalMatOps>::DeclareInput(Level &fineLevel, Level &coarseLevel) const {
    if (implicitTranspose_ == false)
      Input(coarseLevel, "R");

    Input(fineLevel,   "A");
    Input(coarseLevel, "P");

    // call DeclareInput of all user-given transfer factories
    for (std::vector<RCP<const FactoryBase> >::const_iterator it = transferFacts_.begin(); it != transferFacts_.end(); ++it)
      (*it)->CallDeclareInput(coarseLevel);
  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node, class LocalMatOps>
  void RAPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node, LocalMatOps>::Build(Level &fineLevel, Level &coarseLevel) const { // FIXME make fineLevel const
    {
      FactoryMonitor m(*this, "Computing Ac", coarseLevel);

      // Set "Keeps" from params
      const Teuchos::ParameterList& pL = GetParameterList();
      if (pL.isParameter("Keep AP Pattern")  && pL.get<bool>("Keep AP Pattern"))
        coarseLevel.Keep("AP Pattern",  this);
      if (pL.isParameter("Keep RAP Pattern") && pL.get<bool>("Keep RAP Pattern"))
        coarseLevel.Keep("RAP Pattern", this);

      // Extract parameters
      if (pL.isParameter("CheckMainDiagonal") && pL.get<bool>("CheckMainDiagonal"))
        checkAc_ = true;
      if (pL.isParameter("RepairMainDiagonal") && pL.get<bool>("RepairMainDiagonal")) {
        repairZeroDiagonals_ = true;
        checkAc_ = true;
      }

      //
      // Inputs: A, P
      //

      RCP<Matrix> A = Get< RCP<Matrix> >(fineLevel,   "A");
      RCP<Matrix> P = Get< RCP<Matrix> >(coarseLevel, "P");

      //
      // Build Ac = RAP
      //

      RCP<Matrix> AP;

      // Reuse pattern if available (multiple solve)
      if (coarseLevel.IsAvailable("AP Pattern", this)){
        GetOStream(Runtime0, 0) << "Ac: Using previous AP pattern"<<std::endl;
        AP = Get< RCP<Matrix> >(coarseLevel, "AP Pattern");
      }

      {
        SubFactoryMonitor subM(*this, "MxM: A x P", coarseLevel);
        AP = Utils::Multiply(*A, false, *P, false, AP, GetOStream(Statistics2,0));
        Set(coarseLevel, "AP Pattern", AP);
      }

      bool doOptimizedStorage = !checkAc_; // Optimization storage option. If not modifying matrix later (inserting local values), allow optimization of storage.
                                           // This is necessary for new faster Epetra MM kernels.

      RCP<Matrix> Ac;

      // Reuse coarse matrix memory if available (multiple solve)
      if (coarseLevel.IsAvailable("RAP Pattern", this)) {
        GetOStream(Runtime0, 0) << "Ac: Using previous RAP pattern" << std::endl;
        Ac = Get< RCP<Matrix> >(coarseLevel, "RAP Pattern");
      }

      if (implicitTranspose_) {
        SubFactoryMonitor m2(*this, "MxM: P' x (AP) (implicit)", coarseLevel);

        Ac = Utils::Multiply(*P, true, *AP, false, Ac, GetOStream(Statistics2,0), true, doOptimizedStorage);

      } else {

        SubFactoryMonitor m2(*this, "MxM: R x (AP) (explicit)", coarseLevel);

        RCP<Matrix> R = Get< RCP<Matrix> >(coarseLevel, "R");
        Ac = Utils::Multiply(*R, false, *AP, false, Ac, GetOStream(Statistics2,0), true, doOptimizedStorage);

      }

      if (checkAc_)
        CheckMainDiagonal(Ac);

      RCP<ParameterList> params = rcp(new ParameterList());;
      params->set("printLoadBalancingInfo", true);
      GetOStream(Statistics1, 0) << Utils::PrintMatrixInfo(*Ac, "Ac", params);

      Set(coarseLevel, "A",           Ac);
      Set(coarseLevel, "RAP Pattern", Ac);
    }

    if (transferFacts_.begin() != transferFacts_.end()) {
      SubFactoryMonitor m(*this, "Projections", coarseLevel);

      // call Build of all user-given transfer factories
      for (std::vector<RCP<const FactoryBase> >::const_iterator it = transferFacts_.begin(); it != transferFacts_.end(); ++it) {
        RCP<const FactoryBase> fac = *it;
        GetOStream(Runtime0, 0) << "RAPFactory: call transfer factory: " << fac->description() << std::endl;
        fac->CallBuild(coarseLevel);
        // Coordinates transfer is marginally different from all other operations
        // because it is *optional*, and not required. For instance, we may need
        // coordinates only on level 4 if we start repartitioning from that level,
        // but we don't need them on level 1,2,3. As our current Hierarchy setup
        // assumes propagation of dependencies only through three levels, this
        // means that we need to rely on other methods to propagate optional data.
        //
        // The method currently used is through RAP transfer factories, which are
        // simply factories which are called at the end of RAP with a single goal:
        // transfer some fine data to coarser level. Because these factories are
        // kind of outside of the mainline factories, they behave different. In
        // particular, we call their Build method explicitly, rather than through
        // Get calls. This difference is significant, as the Get call is smart
        // enough to know when to release all factory dependencies, and Build is
        // dumb. This led to the following CoordinatesTransferFactory sequence:
        // 1. Request level 0
        // 2. Request level 1
        // 3. Request level 0
        // 4. Release level 0
        // 5. Release level 1
        //
        // The problem is missing "6. Release level 0". Because it was missing,
        // we had outstanding request on "Coordinates", "Aggregates" and
        // "CoarseMap" on level 0.
        //
        // This was fixed by explicitly calling Release on transfer factories in
        // RAPFactory. I am still unsure how exactly it works, but now we have
        // clear data requests for all levels.
        coarseLevel.Release(*fac);
      }
    }

  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node, class LocalMatOps>
  void RAPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node, LocalMatOps>::CheckMainDiagonal(RCP<Matrix> & Ac) const {
    // plausibility check: no zeros on diagonal
    RCP<Vector> diagVec = VectorFactory::Build(Ac->getRowMap());
    Ac->getLocalDiagCopy(*diagVec);

    SC zero = Teuchos::ScalarTraits<SC>::zero(), one = Teuchos::ScalarTraits<SC>::one();

    Teuchos::RCP<Teuchos::ParameterList> p = Teuchos::rcp(new Teuchos::ParameterList());
    p->set("DoOptimizeStorage",true);

    RCP<Matrix> fixDiagMatrix = Teuchos::null;
    if (repairZeroDiagonals_) fixDiagMatrix = MatrixFactory::Build(Ac->getRowMap(), 1);

    LO lZeroDiags = 0;
    Teuchos::ArrayRCP< Scalar > diagVal = diagVec->getDataNonConst(0);
    for (size_t r = 0; r < Ac->getRowMap()->getNodeNumElements(); r++) {
      if (diagVal[r] == zero) {
        lZeroDiags++;

        if (repairZeroDiagonals_) {
          GO grid = Ac->getRowMap()->getGlobalElement(r);
          Teuchos::ArrayRCP<GO> indout(1,grid);
          Teuchos::ArrayRCP<SC> valout(1, one);
          fixDiagMatrix->insertGlobalValues(grid,indout.view(0, 1), valout.view(0, 1));
        }
      }
    }

    if (repairZeroDiagonals_) {
      Ac->fillComplete(p);
      MueLu::Utils2<Scalar, LocalOrdinal, GlobalOrdinal, Node, LocalMatOps>::TwoMatrixAdd(*Ac, false, 1.0, *fixDiagMatrix, 1.0);

      if (Ac->IsView("stridedMaps")) fixDiagMatrix->CreateView("stridedMaps", Ac);

      Ac = Teuchos::null;     // free singular coarse level matrix
      Ac = fixDiagMatrix;     // set fixed non-singular coarse level matrix
    }

    // call fillComplete with optimized storage option set to true
    // This is necessary for new faster Epetra MM kernels.
    Ac->fillComplete(p);

    // print some output
    if (IsPrint(Warnings0)) {
      const RCP<const Teuchos::Comm<int> > & comm = Ac->getRowMap()->getComm();
      GO lZeroDiagsGO = Teuchos::as<GO>(lZeroDiags); /* LO->GO conversion */
      GO gZeroDiags   = 0;
      sumAll(comm, lZeroDiagsGO, gZeroDiags);
      if (repairZeroDiagonals_) GetOStream(Warnings0,0) << "RAPFactory (WARNING): repaired " << gZeroDiags << " zeros on main diagonal of Ac." << std::endl;
      else                      GetOStream(Warnings0,0) << "RAPFactory (WARNING): found "    << gZeroDiags << " zeros on main diagonal of Ac." << std::endl;
    }

#if 0 // only for debugging
    // check whether Ac has been repaired...
    Ac->getLocalDiagCopy(*diagVec);
    Teuchos::ArrayRCP< Scalar > diagVal2 = diagVec->getDataNonConst(0);
    for (size_t r = 0; r < Ac->getRowMap()->getNodeNumElements(); r++) {
      if (diagVal2[r] == zero) {
        std::cout << "Error: there are zeros left on diagonal after repair..." << std::endl;
      }
    }
#endif

  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node, class LocalMatOps>
  void RAPFactory<Scalar, LocalOrdinal, GlobalOrdinal, Node, LocalMatOps>::AddTransferFactory(const RCP<const FactoryBase>& factory) {
    // check if it's a TwoLevelFactoryBase based transfer factory
    TEUCHOS_TEST_FOR_EXCEPTION(Teuchos::rcp_dynamic_cast<const TwoLevelFactoryBase>(factory) == Teuchos::null, Exceptions::BadCast,
                               "MueLu::RAPFactory::AddTransferFactory: Transfer factory is not derived from TwoLevelFactoryBase. "
                               "This is very strange. (Note: you can remove this exception if there's a good reason for)");
    TEUCHOS_TEST_FOR_EXCEPTION(hasDeclaredInput_, Exceptions::RuntimeError, "MueLu::RAPFactory::AddTransferFactory: Factory is being added after we have already declared input");
    transferFacts_.push_back(factory);
  }

} //namespace MueLu

#define MUELU_RAPFACTORY_SHORT
#endif // MUELU_RAPFACTORY_DEF_HPP