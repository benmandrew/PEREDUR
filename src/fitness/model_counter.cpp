#include "fitness/model_counter.hpp"

#include <cassert>
#include <string>

#include "profile.hpp"

namespace {

Count checked_add(Count lhs, Count rhs) {
    Count result = 0;
    [[maybe_unused]] const bool overflow = count_add_overflow(lhs, rhs, result);
    assert(!overflow);
    return result;
}

Count checked_mul(Count lhs, Count rhs) {
    Count result = 0;
    [[maybe_unused]] const bool overflow = count_mul_overflow(lhs, rhs, result);
    assert(!overflow);
    return result;
}

CountMatrix checked_matrix_multiply(const CountMatrix& lhs,
                                    const CountMatrix& rhs) {
    assert(lhs.cols() == rhs.rows());
    CountMatrix product(lhs.rows(), rhs.cols());
    product.setZero();
    for (Eigen::Index row = 0; row < lhs.rows(); ++row) {
        for (Eigen::Index inner = 0; inner < lhs.cols(); ++inner) {
            const Count left = lhs(row, inner);
            if (left == 0) {
                continue;
            }
            for (Eigen::Index column = 0; column < rhs.cols(); ++column) {
                const Count right = rhs(inner, column);
                if (right == 0) {
                    continue;
                }
                const Count term = checked_mul(left, right);
                product(row, column) = checked_add(product(row, column), term);
            }
        }
    }
    return product;
}

CountVector checked_matrix_vector_multiply(const CountMatrix& matrix,
                                           const CountVector& vector) {
    assert(matrix.cols() == vector.rows());
    CountVector result(matrix.rows());
    result.setZero();
    for (Eigen::Index row = 0; row < matrix.rows(); ++row) {
        Count sum = 0;
        for (Eigen::Index column = 0; column < matrix.cols(); ++column) {
            const Count lhs = matrix(row, column);
            const Count rhs = vector(column);
            if (lhs == 0 || rhs == 0) {
                continue;
            }
            const Count term = checked_mul(lhs, rhs);
            sum = checked_add(sum, term);
        }
        result(row) = sum;
    }
    return result;
}

Count checked_dot_product(const CountVector& lhs, const CountVector& rhs) {
    assert(lhs.rows() == rhs.rows());
    Count sum = 0;
    for (Eigen::Index index = 0; index < lhs.rows(); ++index) {
        const Count left = lhs(index);
        const Count right = rhs(index);
        if (left == 0 || right == 0) {
            continue;
        }
        const Count term = checked_mul(left, right);
        sum = checked_add(sum, term);
    }
    return sum;
}

CountMatrix matrix_power(const CountMatrix& matrix, std::size_t exponent) {
    assert(matrix.rows() == matrix.cols());
    CountMatrix result = CountMatrix::Identity(matrix.rows(), matrix.cols());
    CountMatrix factor = matrix;
    while (exponent > 0) {
        if ((exponent & 1U) != 0U) {
            result = checked_matrix_multiply(result, factor);
        }
        exponent >>= 1U;
        if (exponent > 0) {
            factor = checked_matrix_multiply(factor, factor);
        }
    }
    return result;
}

}  // namespace

Count count_traces(const TransferSystem& system, std::size_t step_count) {
    PEREDUR_PROFILE_SCOPE("count/count_traces");
    const CountMatrix weighted_transition = weighted_transition_matrix(system);
    assert(weighted_transition.rows() == weighted_transition.cols());
    assert(weighted_transition.rows() != 0);
    const CountMatrix propagated =
        matrix_power(weighted_transition, step_count);
    CountVector start(weighted_transition.rows());
    start.setZero();
    start(0) = 1;
    CountVector final_mask(weighted_transition.rows());
    if (system.m_final_state_mask.size() == 0) {
        final_mask.setOnes();
    } else {
        final_mask = system.m_final_state_mask;
    }
    const CountVector propagated_mask =
        checked_matrix_vector_multiply(propagated, final_mask);
    return checked_dot_product(start, propagated_mask);
}
