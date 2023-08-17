#pragma once
#ifndef LDSO_ACCUMULATED_TOP_HESSIAN_H_
#define LDSO_ACCUMULATED_TOP_HESSIAN_H_

#include "NumTypes.h"
#include "Settings.h"

#include "internal/PointHessian.h"
#include "internal/IndexThreadReduce.h"
#include "internal/OptimizationBackend/MatrixAccumulators.h"

using namespace std;

namespace ldso
{

    namespace internal
    {

        class EnergyFunctional;

        class AccumulatedTopHessianSSE
        {
        public:
            EIGEN_MAKE_ALIGNED_OPERATOR_NEW;

            inline AccumulatedTopHessianSSE()
            {
                for (int tid = 0; tid < NUM_THREADS; tid++)
                {
                    nres[tid] = 0;
                    acc[tid] = 0;
                    nframes[tid] = 0;
                }
            };

            inline ~AccumulatedTopHessianSSE()
            {
                for (int tid = 0; tid < NUM_THREADS; tid++)
                {
                    if (acc[tid] != 0)
                        delete[] acc[tid];
                }
            };

            inline void setZero(int nFrames, int min = 0, int max = 1, Vec10 *stats = 0, int tid = 0)
            {

                if (nFrames != nframes[tid])
                {
                    if (acc[tid] != 0)
                        delete[] acc[tid];
#if USE_XI_MODEL
                    acc[tid] = new Accumulator14[nFrames * nFrames];
#else
                    acc[tid] = new AccumulatorApprox[nFrames * nFrames];
#endif
                }

                for (int i = 0; i < nFrames * nFrames; i++)
                {
                    acc[tid][i].initialize();
                }

                nframes[tid] = nFrames;
                nres[tid] = 0;
            }

            void stitchDouble(MatXX &H, VecX &b, EnergyFunctional const *const EF, bool usePrior, bool useDelta,
                              int tid = 0);

            template <int mode>
            void addPoint(shared_ptr<PointHessian> p, EnergyFunctional const *const ef, int tid = 0);

            void stitchDoubleMT(IndexThreadReduce<Vec10> *red, MatXX &H, VecX &b, EnergyFunctional const *const EF,
                                bool usePrior, bool MT)
            {
                // sum up, splitting by bock in square.
                if (MT)
                {
                    MatXX Hs[NUM_THREADS];
                    VecX bs[NUM_THREADS];
                    for (int i = 0; i < NUM_THREADS; i++)
                    {
                        assert(nframes[0] == nframes[i]);
                        Hs[i] = MatXX::Zero(nframes[0] * 8 + CPARS, nframes[0] * 8 + CPARS);
                        bs[i] = VecX::Zero(nframes[0] * 8 + CPARS);
                    }

                    red->reduce(bind(&AccumulatedTopHessianSSE::stitchDoubleInternal,
                                     this, Hs, bs, EF, usePrior, placeholders::_1, placeholders::_2, placeholders::_3, placeholders::_4),
                                0, nframes[0] * nframes[0], 0);

                    // sum up results
                    H = Hs[0];
                    b = bs[0];

                    for (int i = 1; i < NUM_THREADS; i++)
                    {
                        H.noalias() += Hs[i];
                        b.noalias() += bs[i];
                        nres[0] += nres[i];
                    }
                }
                else
                {
                    H = MatXX::Zero(nframes[0] * 8 + CPARS, nframes[0] * 8 + CPARS);
                    b = VecX::Zero(nframes[0] * 8 + CPARS);
                    stitchDoubleInternal(&H, &b, EF, usePrior, 0, nframes[0] * nframes[0], 0, -1);
                }

                // make diagonal by copying over parts.
                for (int h = 0; h < nframes[0]; h++)
                {
                    int hIdx = CPARS + h * 8;
                    H.block<CPARS, 8>(0, hIdx).noalias() = H.block<8, CPARS>(hIdx, 0).transpose();

                    for (int t = h + 1; t < nframes[0]; t++)
                    {
                        int tIdx = CPARS + t * 8;
                        H.block<8, 8>(hIdx, tIdx).noalias() += H.block<8, 8>(tIdx, hIdx).transpose();
                        H.block<8, 8>(tIdx, hIdx).noalias() = H.block<8, 8>(hIdx, tIdx).transpose();
                    }
                }
            }

            int nframes[NUM_THREADS];
            EIGEN_ALIGN16 AccumulatorApprox *acc[NUM_THREADS];
            int nres[NUM_THREADS];

            template <int mode>
            inline void addPointsInternal(
                std::vector<shared_ptr<PointHessian>> *points, EnergyFunctional const *const ef,
                int min = 0, int max = 1, Vec10 *stats = 0, int tid = 0)
            {
                for (int i = min; i < max; i++)
                    addPoint<mode>((*points)[i], ef, tid);
            }

        private:
            void stitchDoubleInternal(
                MatXX *H, VecX *b, EnergyFunctional const *const EF, bool usePrior,
                int min, int max, Vec10 *stats, int tid);
        };
    }
}

#endif