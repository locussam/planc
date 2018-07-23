/* Copyright Ramakrishnan Kannan 2018 */

#ifndef DISTNTF_DISTNTFAOADMM_HPP_
#define DISTNTF_DISTNTFAOADMM_HPP_

#include "distntf/distauntf.hpp"

namespace planc {

class DistNTFAOADMM : public DistAUNTF {
 private:
  // ADMM auxiliary variables
  NCPFactors *m_local_ncp_aux;
  NCPFactors *m_local_ncp_aux_t;
  NCPFactors *m_temp_local_ncp_aux_t;
  MAT L;
  MAT Lt;
  MAT tempgram;
  int admm_iter;
  double tolerance;

 protected:
  MAT update(const int mode) {
    // return variable
    MAT updated_fac(this->m_local_ncp_factors.factor(mode));
    MAT prev_fac = updated_fac;

    // Set up ADMM iteration
    double alpha =
        arma::trace(this->global_gram) / this->m_local_ncp_factors.rank();
    alpha = (alpha > 0) ? alpha : 0.01;
    tempgram = this->global_gram;
    tempgram.diag() += alpha;
    L = arma::chol(tempgram, "lower");
    Lt = L.t();
    bool stop_iter = false;

    m_local_ncp_aux->set(mode, updated_fac);

    // Start ADMM loop from here
    for (int i = 0; i < admm_iter && !stop_iter; i++) {
      prev_fac = updated_fac;
      m_local_ncp_aux->set(mode, updated_fac);
      m_local_ncp_aux_t->set(mode, m_local_ncp_aux->factor(mode).t());

      m_temp_local_ncp_aux_t->set(
          mode, arma::solve(arma::trimatl(L),
                            this->ncp_local_mttkrp_t[mode] +
                                (alpha * (updated_fac.t() +
                                          m_local_ncp_aux_t->factor(mode)))));
      m_local_ncp_aux_t->set(
          mode,
          arma::solve(arma::trimatu(Lt), m_temp_local_ncp_aux_t->factor(mode)));

      // Update factor matrix
      updated_fac = m_local_ncp_aux_t->factor(mode).t();
      fixNumericalError<MAT>(&(updated_fac), EPSILON_1EMINUS16);
      updated_fac = updated_fac - m_local_ncp_aux->factor(mode);
      updated_fac.for_each(
          [](MAT::elem_type &val) { val = val > 0.0 ? val : 0.0; });

      // Update dual variable
      m_local_ncp_aux->set(mode, m_local_ncp_aux->factor(mode) + updated_fac -
                                     m_local_ncp_aux_t->factor(mode).t());

      // factor norm
      double local_facnorm = arma::norm(updated_fac, "fro");
      local_facnorm *= local_facnorm;

      double global_facnorm = 0.0;
      MPI_Allreduce(&local_facnorm, &global_facnorm, 1, MPI_DOUBLE, MPI_SUM,
                    MPI_COMM_WORLD);
      global_facnorm = sqrt(global_facnorm);
      // dual norm
      double local_dualnorm = arma::norm(updated_fac, "fro");
      local_dualnorm *= local_dualnorm;

      double global_dualnorm = 0.0;
      MPI_Allreduce(&local_dualnorm, &global_dualnorm, 1, MPI_DOUBLE, MPI_SUM,
                    MPI_COMM_WORLD);
      global_dualnorm = sqrt(global_dualnorm);
      // Check stopping criteria (needs communication)
      double r = norm(updated_fac - m_local_ncp_aux->factor(mode), "fro");
      r *= r;
      double global_r = 0.0;
      MPI_Allreduce(&r, &global_r, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      global_r = sqrt(global_r);
      double s = norm(updated_fac - prev_fac, "fro");
      s *= s;
      double global_s = 0.0;
      MPI_Allreduce(&s, &global_s, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
      global_s = sqrt(global_s);
      if (global_r < (tolerance * global_facnorm) &&
          global_s < (tolerance * global_dualnorm))
        stop_iter = true;
    }
    m_local_ncp_aux->distributed_normalize(mode);
    return updated_fac.t();
  }

 public:
  DistNTFAOADMM(const Tensor &i_tensor, const int i_k, algotype i_algo,
                const UVEC &i_global_dims, const UVEC &i_local_dims,
                const NTFMPICommunicator &i_mpicomm)
      : DistAUNTF(i_tensor, i_k, i_algo, i_global_dims, i_local_dims,
                  i_mpicomm) {
    m_local_ncp_aux = new NCPFactors(i_local_dims, i_k, false);
    m_local_ncp_aux->zeros();
    m_local_ncp_aux_t = new NCPFactors(i_local_dims, i_k, true);
    m_local_ncp_aux_t->zeros();
    m_temp_local_ncp_aux_t = new NCPFactors(i_local_dims, i_k, true);
    m_temp_local_ncp_aux_t->zeros();
    L.zeros(i_k, i_k);
    Lt.zeros(i_k, i_k);
    tempgram.zeros(i_k, i_k);
    admm_iter = 5;
    tolerance = 0.01;
  }
};  // class DistNTFAOADMM

}  // namespace planc

#endif  // DISTNTF_DISTNTFAOADMM_HPP_
