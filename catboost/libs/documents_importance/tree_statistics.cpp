#include "tree_statistics.h"
#include "ders_helpers.h"

#include <catboost/libs/algo/index_calcer.h>

// ITreeStatisticsEvaluator

TVector<TTreeStatistics> ITreeStatisticsEvaluator::EvaluateTreeStatistics(
    const TFullModel& model,
    const TPool& pool
) {
    NJson::TJsonValue paramsJson = ReadTJsonValue(model.ModelInfo.at("params"));
    const ELossFunction lossFunction = FromString<ELossFunction>(paramsJson["loss_function"]["type"].GetString());
    const ELeavesEstimation leafEstimationMethod = FromString<ELeavesEstimation>(paramsJson["tree_learner_options"]["leaf_estimation_method"].GetString());
    const ui32 leavesEstimationIterations = paramsJson["tree_learner_options"]["leaf_estimation_iterations"].GetUInteger();
    const float learningRate = paramsJson["boosting_options"]["learning_rate"].GetDouble();
    const float l2LeafReg = paramsJson["tree_learner_options"]["l2_leaf_reg"].GetDouble();
    const ui32 treeCount = model.ObliviousTrees.GetTreeCount();

    const TVector<ui8> binarizedFeatures = BinarizeFeatures(model, pool);
    TVector<TTreeStatistics> treeStatistics;
    treeStatistics.reserve(treeCount);
    TVector<double> approxes(DocCount);
    for (ui32 treeId = 0; treeId < treeCount; ++treeId) {
        LeafCount = 1 << model.ObliviousTrees.TreeSizes[treeId];
        LeafIndices = BuildIndicesForBinTree(model, binarizedFeatures, treeId);

        TVector<TVector<ui32>> leavesDocId(LeafCount);
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            leavesDocId[LeafIndices[docId]].push_back(docId);
        }


        TVector<TVector<double>> leafValues(leavesEstimationIterations);
        TVector<TVector<double>> formulaDenominators(leavesEstimationIterations);
        TVector<TVector<double>> formulaNumeratorAdding(leavesEstimationIterations);
        TVector<TVector<double>> formulaNumeratorMultiplier(leavesEstimationIterations);
        for (ui32 it = 0; it < leavesEstimationIterations; ++it) {
            EvaluateDerivatives(
                lossFunction,
                leafEstimationMethod,
                approxes,
                pool,
                &FirstDerivatives,
                &SecondDerivatives,
                &ThirdDerivatives
            );

            const TVector<double> leafNumerators = ComputeLeafNumerators(pool.Docs.Weight);
            TVector<double> leafDenominators = ComputeLeafDenominators(pool.Docs.Weight, l2LeafReg);
            LeafValues.resize(LeafCount);
            for (ui32 leafId = 0; leafId < LeafCount; ++leafId) {
                LeafValues[leafId] = -leafNumerators[leafId] / leafDenominators[leafId] * learningRate;
            }
            formulaNumeratorAdding[it] = ComputeFormulaNumeratorAdding(learningRate);
            formulaNumeratorMultiplier[it] = ComputeFormulaNumeratorMultiplier(pool.Docs.Weight, learningRate);
            formulaDenominators[it].swap(leafDenominators);

            for (ui32 docId = 0; docId < DocCount; ++docId) {
                approxes[docId] += LeafValues[LeafIndices[docId]];
            }
            leafValues[it].swap(LeafValues);
        }

        treeStatistics.push_back({
            LeafCount,
            LeafIndices,
            leavesDocId,
            leafValues,
            formulaDenominators,
            formulaNumeratorAdding,
            formulaNumeratorMultiplier
        });
    }
    return treeStatistics;
}

// TGradientTreeStatisticsEvaluator

TVector<double> TGradientTreeStatisticsEvaluator::ComputeLeafNumerators(const TVector<float>& weights) {
    TVector<double> leafNumerators(LeafCount);
    if (weights.empty()) {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            leafNumerators[LeafIndices[docId]] += FirstDerivatives[docId];
        }
    } else {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            leafNumerators[LeafIndices[docId]] += weights[docId] * FirstDerivatives[docId];
        }
    }
    return leafNumerators;
}

TVector<double> TGradientTreeStatisticsEvaluator::ComputeLeafDenominators(const TVector<float>& weights, float l2LeafReg) {
    TVector<double> leafDenominators(LeafCount);
    if (weights.empty()) {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            leafDenominators[LeafIndices[docId]] += 1;
        }
    } else {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            leafDenominators[LeafIndices[docId]] += weights[docId];
        }
    }
    for (ui32 leafId = 0; leafId < LeafCount; ++leafId) {
        leafDenominators[leafId] += l2LeafReg;
    }
    return leafDenominators;
}

TVector<double> TGradientTreeStatisticsEvaluator::ComputeFormulaNumeratorAdding(float learningRate) {
    TVector<double> formulaNumeratorAdding(DocCount);
    for (ui32 docId = 0; docId < DocCount; ++docId) {
        formulaNumeratorAdding[docId] = LeafValues[LeafIndices[docId]] / learningRate + FirstDerivatives[docId];
    }
    return formulaNumeratorAdding;
}

TVector<double> TGradientTreeStatisticsEvaluator::ComputeFormulaNumeratorMultiplier(const TVector<float>& weights, float /*learningRate*/) {
    TVector<double> formulaNumeratorMultiplier(DocCount);
    if (weights.empty()) {
        formulaNumeratorMultiplier = SecondDerivatives;
    } else {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            formulaNumeratorMultiplier[docId] = weights[docId] * SecondDerivatives[docId];
        }
    }
    return formulaNumeratorMultiplier;
}

// TNewtonTreeStatisticsEvaluator

TVector<double> TNewtonTreeStatisticsEvaluator::ComputeLeafNumerators(const TVector<float>& weights) {
    TVector<double> leafNumerators(LeafCount);
    if (weights.empty()) {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            leafNumerators[LeafIndices[docId]] += FirstDerivatives[docId];
        }
    } else {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            leafNumerators[LeafIndices[docId]] += weights[docId] * FirstDerivatives[docId];
        }
    }
    return leafNumerators;
}

TVector<double> TNewtonTreeStatisticsEvaluator::ComputeLeafDenominators(const TVector<float>& weights, float l2LeafReg) {
    TVector<double> leafDenominators(LeafCount);
    if (weights.empty()) {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            leafDenominators[LeafIndices[docId]] += SecondDerivatives[docId];
        }
    } else {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            leafDenominators[LeafIndices[docId]] += weights[docId] * SecondDerivatives[docId];
        }
    }
    for (ui32 leafId = 0; leafId < LeafCount; ++leafId) {
        leafDenominators[leafId] += l2LeafReg;
    }
    return leafDenominators;
}

TVector<double> TNewtonTreeStatisticsEvaluator::ComputeFormulaNumeratorAdding(float learningRate) {
    TVector<double> formulaNumeratorAdding(DocCount);
    for (ui32 docId = 0; docId < DocCount; ++docId) {
        formulaNumeratorAdding[docId] = LeafValues[LeafIndices[docId]] * SecondDerivatives[docId] / learningRate + FirstDerivatives[docId];
    }
    return formulaNumeratorAdding;
}

TVector<double> TNewtonTreeStatisticsEvaluator::ComputeFormulaNumeratorMultiplier(const TVector<float>& weights, float learningRate) {
    TVector<double> formulaNumeratorMultiplier(DocCount);
    if (weights.empty()) {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            formulaNumeratorMultiplier[docId] = LeafValues[LeafIndices[docId]] * ThirdDerivatives[docId] / learningRate + SecondDerivatives[docId];
        }
    } else {
        for (ui32 docId = 0; docId < DocCount; ++docId) {
            formulaNumeratorMultiplier[docId] = weights[docId] * (LeafValues[LeafIndices[docId]] * ThirdDerivatives[docId] / learningRate + SecondDerivatives[docId]);
        }
    }
    return formulaNumeratorMultiplier;
}
