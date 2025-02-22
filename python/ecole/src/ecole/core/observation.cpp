#include <algorithm>
#include <memory>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <xtensor-python/pytensor.hpp>

#include "ecole/observation/hutter-2011.hpp"
#include "ecole/observation/khalil-2016.hpp"
#include "ecole/observation/milp-bipartite.hpp"
#include "ecole/observation/node-bipartite.hpp"
#include "ecole/observation/nothing.hpp"
#include "ecole/observation/pseudocosts.hpp"
#include "ecole/observation/strong-branching-scores.hpp"

#include "ecole/observation/capacity.hpp"
#include "ecole/observation/focusnode.hpp"
#include "ecole/observation/weight.hpp"

#include "ecole/python/auto-class.hpp"
#include "ecole/scip/model.hpp"
#include "ecole/utility/sparse-matrix.hpp"

#include "core.hpp"

namespace ecole::observation {

namespace py = pybind11;

/**
 * Helper function to bind the `before_reset` method of observation functions.
 */
template <typename PyClass, typename... Args> auto def_before_reset(PyClass pyclass, Args&&... args) {
	return pyclass.def(
		"before_reset",
		&PyClass::type::before_reset,
		py::arg("model"),
		py::call_guard<py::gil_scoped_release>(),
		std::forward<Args>(args)...);
}

/**
 * Helper function to bind the `extract` method of observation functions.
 */
template <typename PyClass, typename... Args> auto def_extract(PyClass pyclass, Args&&... args) {
	return pyclass.def(
		"extract",
		&PyClass::type::extract,
		py::arg("model"),
		py::arg("done"),
		py::call_guard<py::gil_scoped_release>(),
		std::forward<Args>(args)...);
}

/**
 * Observation module bindings definitions.
 */
void bind_submodule(py::module_ const& m) {
	m.doc() = "Observation classes for Ecole.";

	xt::import_numpy();

	m.attr("Nothing") = py::type::of<Nothing>();

	using coo_matrix = decltype(NodeBipartiteObs::edge_features);
	ecole::python::auto_class<coo_matrix>(m, "coo_matrix", R"(
		Sparse matrix in the coordinate format.

		Similar to Scipy's ``scipy.sparse.coo_matrix`` or PyTorch ``torch.sparse``.
	)")
		.def_auto_copy()
		.def_auto_pickle("values", "indices", "shape")
		.def_readwrite_xtensor("values", &coo_matrix::values, "A vector of non zero values in the matrix")
		.def_readwrite_xtensor("indices", &coo_matrix::indices, R"(
			A matrix holding the indices of non zero coefficient in the sparse matrix.

			There are as many columns as there are non zero coefficients, and each row is a
			dimension in the sparse matrix.
		)")
		.def_readwrite("shape", &coo_matrix::shape, "The dimension of the sparse matrix, as if it was dense.")
		.def_property_readonly("nnz", &coo_matrix::nnz);

	// Node bipartite observation
	auto node_bipartite_obs =
		ecole::python::auto_class<NodeBipartiteObs>(m, "NodeBipartiteObs", R"(
		Bipartite graph observation for branch-and-bound nodes.

		The optimization problem is represented as an heterogenous bipartite graph.
		On one side, a node is associated with one variable, on the other side a node is
		associated with one LP row.
		There exist an edge between a variable and a constraint if the variable exists in the
		constraint with a non-zero coefficient.

		Each variable and constraint node is associated with a vector of features.
		Each edge is associated with the coefficient of the variable in the constraint.
	)")
			.def_auto_copy()
			.def_auto_pickle("variable_features", "row_features", "edge_features")
			.def_readwrite_xtensor("variable_features", &NodeBipartiteObs::variable_features, R"rst(
					A matrix where each row represents a variable, and each column a feature of the variable.

					Variables are ordered according to their position in the original problem (``SCIPvarGetProbindex``),
					hence they can be indexed by the :py:class:`~ecole.environment.Branching` environment ``action_set``.
				)rst")
			.def_readwrite_xtensor(
				"row_features",
				&NodeBipartiteObs::row_features,
				"A matrix where each row is represents a constraint, and each column a feature of the constraints.")
			.def_readwrite(
				"edge_features",
				&NodeBipartiteObs::edge_features,
				"The constraint matrix of the optimization problem, with rows for contraints and "
				"columns for variables.");

	py::enum_<NodeBipartiteObs::VariableFeatures>(node_bipartite_obs, "VariableFeatures")
		.value("objective", NodeBipartiteObs::VariableFeatures::objective)
		.value("is_type_binary", NodeBipartiteObs::VariableFeatures::is_type_binary)
		.value("is_type_integer", NodeBipartiteObs::VariableFeatures::is_type_integer)
		.value("is_type_implicit_integer", NodeBipartiteObs::VariableFeatures::is_type_implicit_integer)
		.value("is_type_continuous", NodeBipartiteObs::VariableFeatures::is_type_continuous)
		.value("has_lower_bound", NodeBipartiteObs::VariableFeatures::has_lower_bound)
		.value("has_upper_bound", NodeBipartiteObs::VariableFeatures::has_upper_bound)
		.value("normed_reduced_cost", NodeBipartiteObs::VariableFeatures::normed_reduced_cost)
		.value("solution_value", NodeBipartiteObs::VariableFeatures::solution_value)
		.value("solution_frac", NodeBipartiteObs::VariableFeatures::solution_frac)
		.value("is_solution_at_lower_bound", NodeBipartiteObs::VariableFeatures::is_solution_at_lower_bound)
		.value("is_solution_at_upper_bound", NodeBipartiteObs::VariableFeatures::is_solution_at_upper_bound)
		.value("scaled_age", NodeBipartiteObs::VariableFeatures::scaled_age)
		.value("incumbent_value", NodeBipartiteObs::VariableFeatures::incumbent_value)
		.value("average_incumbent_value", NodeBipartiteObs::VariableFeatures::average_incumbent_value)
		.value("is_basis_lower", NodeBipartiteObs::VariableFeatures::is_basis_lower)
		.value("is_basis_basic", NodeBipartiteObs::VariableFeatures::is_basis_basic)
		.value("is_basis_upper", NodeBipartiteObs::VariableFeatures::is_basis_upper)
		.value("is_basis_zero", NodeBipartiteObs::VariableFeatures ::is_basis_zero);

	py::enum_<NodeBipartiteObs::RowFeatures>(node_bipartite_obs, "RowFeatures")
		.value("bias", NodeBipartiteObs::RowFeatures::bias)
		.value("objective_cosine_similarity", NodeBipartiteObs::RowFeatures::objective_cosine_similarity)
		.value("is_tight", NodeBipartiteObs::RowFeatures::is_tight)
		.value("dual_solution_value", NodeBipartiteObs::RowFeatures::dual_solution_value)
		.value("scaled_age", NodeBipartiteObs::RowFeatures::scaled_age);

	auto node_bipartite = py::class_<NodeBipartite>(m, "NodeBipartite", R"(
		Bipartite graph observation function on branch-and bound node.

		This observation function extract structured :py:class:`NodeBipartiteObs`.
	)");
	node_bipartite.def(py::init<bool>(), py::arg("cache") = false, R"(
		Constructor for NodeBipartite.

		Parameters
		----------
		cache :
			Whether or not to cache static features within an episode.
			Currently, this is only safe if cutting planes are disabled.
	)");
	def_before_reset(node_bipartite, "Cache some feature not expected to change during an episode.");
	def_extract(node_bipartite, "Extract a new :py:class:`NodeBipartiteObs`.");

	// MILP bipartite observation
	auto milp_bipartite_obs =
		ecole::python::auto_class<MilpBipartiteObs>(m, "MilpBipartiteObs", R"(
		Bipartite graph observation that represents the most recent MILP during presolving.

		The optimization problem is represented as an heterogenous bipartite graph.
		On one side, a node is associated with one variable, on the other side a node is
		associated with one constraint.
		There exist an edge between a variable and a constraint if the variable exists in the
		constraint with a non-zero coefficient.

		Each variable and constraint node is associated with a vector of features.
		Each edge is associated with the coefficient of the variable in the constraint.
	)")
			.def_auto_copy()
			.def_auto_pickle("variable_features", "constraint_features", "edge_features")
			.def_readwrite_xtensor("variable_features", &MilpBipartiteObs::variable_features, R"rst(
					A matrix where each row represents a variable, and each column a feature of the variable.

					Variables are ordered according to their position in the original problem (``SCIPvarGetProbindex``),
					hence they can be indexed by the :py:class:`~ecole.environment.Branching` environment ``action_set``.
				)rst")
			.def_readwrite_xtensor(
				"constraint_features",
				&MilpBipartiteObs::constraint_features,
				"A matrix where each row is represents a constraint, and each column a feature of the constraints.")
			.def_readwrite(
				"edge_features",
				&MilpBipartiteObs::edge_features,
				"The constraint matrix of the optimization problem, with rows for contraints and columns for variables.");

	py::enum_<MilpBipartiteObs::VariableFeatures>(milp_bipartite_obs, "VariableFeatures")
		.value("objective", MilpBipartiteObs::VariableFeatures::objective)
		.value("is_type_binary", MilpBipartiteObs::VariableFeatures::is_type_binary)
		.value("is_type_integer", MilpBipartiteObs::VariableFeatures::is_type_integer)
		.value("is_type_implicit_integer", MilpBipartiteObs::VariableFeatures::is_type_implicit_integer)
		.value("is_type_continuous", MilpBipartiteObs::VariableFeatures::is_type_continuous)
		.value("has_lower_bound", MilpBipartiteObs::VariableFeatures::has_lower_bound)
		.value("has_upper_bound", MilpBipartiteObs::VariableFeatures::has_upper_bound)
		.value("lower_bound", MilpBipartiteObs::VariableFeatures::lower_bound)
		.value("upper_bound", MilpBipartiteObs::VariableFeatures::upper_bound);

	py::enum_<MilpBipartiteObs::ConstraintFeatures>(milp_bipartite_obs, "ConstraintFeatures")
		.value("bias", MilpBipartiteObs::ConstraintFeatures::bias);

	auto milp_bipartite = py::class_<MilpBipartite>(m, "MilpBipartite", R"(
		Bipartite graph observation function for the sub-MILP at the latest branch-and-bound node.

		This observation function extract structured :py:class:`MilpBipartiteObs`.
	)");
	milp_bipartite.def(py::init<bool>(), py::arg("normalize") = false, R"(
		Constructor for MilpBipartite.

		Parameters
		----------
		normalize :
			Should the features be normalized?
			This is recommended for some application such as deep learning models.
	)");
	def_before_reset(milp_bipartite, R"(Do nothing.)");
	def_extract(milp_bipartite, "Extract a new :py:class:`MilpBipartiteObs`.");

	// Strong branching observation
	auto strong_branching_scores = py::class_<StrongBranchingScores>(m, "StrongBranchingScores", R"(
		Strong branching score observation function on branch-and bound node.

		This observation obtains scores for all LP or pseudo candidate variables at a
		branch-and-bound node.
		The strong branching score measures the quality of each variable for branching (higher is better).
		This observation can be used as an expert for imitation learning algorithms.

		This observation function extracts an array containing the strong branching score for
		each variable in the problem.
		Variables are ordered according to their position in the original problem (``SCIPvarGetProbindex``),
		hence they can be indexed by the :py:class:`~ecole.environment.Branching` environment ``action_set``.
		Variables for which a strong branching score is not applicable are filled with ``NaN``.
	)");
	strong_branching_scores.def(py::init<bool>(), py::arg("pseudo_candidates") = false, R"(
		Constructor for StrongBranchingScores.

		Parameters
		----------
		pseudo_candidates :
			The parameter determines if strong branching scores are computed for
			pseudo candidate variables (when true) or LP candidate variables (when false).
	)");
	def_before_reset(strong_branching_scores, R"(Do nothing.)");
	def_extract(strong_branching_scores, "Extract an array containing strong branching scores.");

	// Pseudocosts observation
	auto pseudocosts = py::class_<Pseudocosts>(m, "Pseudocosts", R"(
		Pseudocosts observation function on branch-and-bound nodes.

		This observation obtains pseudocosts for all LP fractional candidate variables at a
		branch-and-bound node.
		The pseudocost is a cheap approximation to the strong branching
		score and measures the quality of branching for each variable.
		This observation can be used as a practical branching strategy by always branching on the
		variable with the highest pseudocost, although in practice is it not as efficient as SCIP's
		default strategy, reliability pseudocost branching (also known as hybrid branching).

		This observation function extracts an array containing the pseudocost for each variable in the problem.
		Variables are ordered according to their position in the original problem (``SCIPvarGetProbindex``),
		hence they can be indexed by the :py:class:`~ecole.environment.Branching` environment ``action_set``.
		Variables for which a pseudocost is not applicable are filled with ``NaN``.
	)");
	pseudocosts.def(py::init<>());
	def_before_reset(pseudocosts, R"(Do nothing.)");
	def_extract(pseudocosts, "Extract an array containing pseudocosts.");

	// Khalil observation
	auto khalil2016_obs = ecole::python::auto_class<Khalil2016Obs>(m, "Khalil2016Obs", R"(
		Branching candidates features from Khalil et al. (2016).

		The observation is a matrix where rows represent all variables and columns represent features related
		to these variables.
		See [Khalil2016]_ for a complete reference on this observation function.

		.. [Khalil2016]
			Khalil, Elias Boutros, Pierre Le Bodic, Le Song, George Nemhauser, and Bistra Dilkina.
			"`Learning to branch in mixed integer programming.
			<https://dl.acm.org/doi/10.5555/3015812.3015920>`_"
			*Thirtieth AAAI Conference on Artificial Intelligence*. 2016.
	)");
	khalil2016_obs.def_auto_copy()
		.def_auto_pickle("features")
		.def_readwrite_xtensor("features", &Khalil2016Obs::features, R"rst(
			A matrix where each row represents a variable, and each column a feature of the variable.

			Variables are ordered according to their position in the original problem (``SCIPvarGetProbindex``),
			hence they can be indexed by the :py:class:`~ecole.environment.Branching` environment ``action_set``.
			Variables for which the features are not applicable are filled with ``NaN``.

			The first :py:attr:`Khalil2016Obs.n_static_features` features columns are static (they do not
			change through the solving process), and the remaining :py:attr:`Khalil2016Obs.n_dynamic_features`
			are dynamic.
		)rst")
		.def_readonly_static("n_static_features", &Khalil2016Obs::n_static_features)
		.def_readonly_static("n_dynamic_features", &Khalil2016Obs::n_dynamic_features);

	py::enum_<Khalil2016Obs::Features>(khalil2016_obs, "Features")
		.value("obj_coef", Khalil2016Obs::Features::obj_coef)
		.value("obj_coef_pos_part", Khalil2016Obs::Features::obj_coef_pos_part)
		.value("obj_coef_neg_part", Khalil2016Obs::Features::obj_coef_neg_part)
		.value("n_rows", Khalil2016Obs::Features::n_rows)
		.value("rows_deg_mean", Khalil2016Obs::Features::rows_deg_mean)
		.value("rows_deg_stddev", Khalil2016Obs::Features::rows_deg_stddev)
		.value("rows_deg_min", Khalil2016Obs::Features::rows_deg_min)
		.value("rows_deg_max", Khalil2016Obs::Features::rows_deg_max)
		.value("rows_pos_coefs_count", Khalil2016Obs::Features::rows_pos_coefs_count)
		.value("rows_pos_coefs_mean", Khalil2016Obs::Features::rows_pos_coefs_mean)
		.value("rows_pos_coefs_stddev", Khalil2016Obs::Features::rows_pos_coefs_stddev)
		.value("rows_pos_coefs_min", Khalil2016Obs::Features::rows_pos_coefs_min)
		.value("rows_pos_coefs_max", Khalil2016Obs::Features::rows_pos_coefs_max)
		.value("rows_neg_coefs_count", Khalil2016Obs::Features::rows_neg_coefs_count)
		.value("rows_neg_coefs_mean", Khalil2016Obs::Features::rows_neg_coefs_mean)
		.value("rows_neg_coefs_stddev", Khalil2016Obs::Features::rows_neg_coefs_stddev)
		.value("rows_neg_coefs_min", Khalil2016Obs::Features::rows_neg_coefs_min)
		.value("rows_neg_coefs_max", Khalil2016Obs::Features::rows_neg_coefs_max)
		.value("slack", Khalil2016Obs::Features::slack)
		.value("ceil_dist", Khalil2016Obs::Features::ceil_dist)
		.value("pseudocost_up", Khalil2016Obs::Features::pseudocost_up)
		.value("pseudocost_down", Khalil2016Obs::Features::pseudocost_down)
		.value("pseudocost_ratio", Khalil2016Obs::Features::pseudocost_ratio)
		.value("pseudocost_sum", Khalil2016Obs::Features::pseudocost_sum)
		.value("pseudocost_product", Khalil2016Obs::Features::pseudocost_product)
		.value("n_cutoff_up", Khalil2016Obs::Features::n_cutoff_up)
		.value("n_cutoff_down", Khalil2016Obs::Features::n_cutoff_down)
		.value("n_cutoff_up_ratio", Khalil2016Obs::Features::n_cutoff_up_ratio)
		.value("n_cutoff_down_ratio", Khalil2016Obs::Features::n_cutoff_down_ratio)
		.value("rows_dynamic_deg_mean", Khalil2016Obs::Features::rows_dynamic_deg_mean)
		.value("rows_dynamic_deg_stddev", Khalil2016Obs::Features::rows_dynamic_deg_stddev)
		.value("rows_dynamic_deg_min", Khalil2016Obs::Features::rows_dynamic_deg_min)
		.value("rows_dynamic_deg_max", Khalil2016Obs::Features::rows_dynamic_deg_max)
		.value("rows_dynamic_deg_mean_ratio", Khalil2016Obs::Features::rows_dynamic_deg_mean_ratio)
		.value("rows_dynamic_deg_min_ratio", Khalil2016Obs::Features::rows_dynamic_deg_min_ratio)
		.value("rows_dynamic_deg_max_ratio", Khalil2016Obs::Features::rows_dynamic_deg_max_ratio)
		.value("coef_pos_rhs_ratio_min", Khalil2016Obs::Features::coef_pos_rhs_ratio_min)
		.value("coef_pos_rhs_ratio_max", Khalil2016Obs::Features::coef_pos_rhs_ratio_max)
		.value("coef_neg_rhs_ratio_min", Khalil2016Obs::Features::coef_neg_rhs_ratio_min)
		.value("coef_neg_rhs_ratio_max", Khalil2016Obs::Features::coef_neg_rhs_ratio_max)
		.value("pos_coef_pos_coef_ratio_min", Khalil2016Obs::Features::pos_coef_pos_coef_ratio_min)
		.value("pos_coef_pos_coef_ratio_max", Khalil2016Obs::Features::pos_coef_pos_coef_ratio_max)
		.value("pos_coef_neg_coef_ratio_min", Khalil2016Obs::Features::pos_coef_neg_coef_ratio_min)
		.value("pos_coef_neg_coef_ratio_max", Khalil2016Obs::Features::pos_coef_neg_coef_ratio_max)
		.value("neg_coef_pos_coef_ratio_min", Khalil2016Obs::Features::neg_coef_pos_coef_ratio_min)
		.value("neg_coef_pos_coef_ratio_max", Khalil2016Obs::Features::neg_coef_pos_coef_ratio_max)
		.value("neg_coef_neg_coef_ratio_min", Khalil2016Obs::Features::neg_coef_neg_coef_ratio_min)
		.value("neg_coef_neg_coef_ratio_max", Khalil2016Obs::Features::neg_coef_neg_coef_ratio_max)
		.value("active_coef_weight1_count", Khalil2016Obs::Features::active_coef_weight1_count)
		.value("active_coef_weight1_sum", Khalil2016Obs::Features::active_coef_weight1_sum)
		.value("active_coef_weight1_mean", Khalil2016Obs::Features::active_coef_weight1_mean)
		.value("active_coef_weight1_stddev", Khalil2016Obs::Features::active_coef_weight1_stddev)
		.value("active_coef_weight1_min", Khalil2016Obs::Features::active_coef_weight1_min)
		.value("active_coef_weight1_max", Khalil2016Obs::Features::active_coef_weight1_max)
		.value("active_coef_weight2_count", Khalil2016Obs::Features::active_coef_weight2_count)
		.value("active_coef_weight2_sum", Khalil2016Obs::Features::active_coef_weight2_sum)
		.value("active_coef_weight2_mean", Khalil2016Obs::Features::active_coef_weight2_mean)
		.value("active_coef_weight2_stddev", Khalil2016Obs::Features::active_coef_weight2_stddev)
		.value("active_coef_weight2_min", Khalil2016Obs::Features::active_coef_weight2_min)
		.value("active_coef_weight2_max", Khalil2016Obs::Features::active_coef_weight2_max)
		.value("active_coef_weight3_count", Khalil2016Obs::Features::active_coef_weight3_count)
		.value("active_coef_weight3_sum", Khalil2016Obs::Features::active_coef_weight3_sum)
		.value("active_coef_weight3_mean", Khalil2016Obs::Features::active_coef_weight3_mean)
		.value("active_coef_weight3_stddev", Khalil2016Obs::Features::active_coef_weight3_stddev)
		.value("active_coef_weight3_min", Khalil2016Obs::Features::active_coef_weight3_min)
		.value("active_coef_weight3_max", Khalil2016Obs::Features::active_coef_weight3_max)
		.value("active_coef_weight4_count", Khalil2016Obs::Features::active_coef_weight4_count)
		.value("active_coef_weight4_sum", Khalil2016Obs::Features::active_coef_weight4_sum)
		.value("active_coef_weight4_mean", Khalil2016Obs::Features::active_coef_weight4_mean)
		.value("active_coef_weight4_stddev", Khalil2016Obs::Features::active_coef_weight4_stddev)
		.value("active_coef_weight4_min", Khalil2016Obs::Features::active_coef_weight4_min)
		.value("active_coef_weight4_max", Khalil2016Obs::Features::active_coef_weight4_max);

	auto khalil2016 = py::class_<Khalil2016>(m, "Khalil2016", R"(
		Branching candidates features from Khalil et al. (2016).

		This observation function extract structured :py:class:`Khalil2016Obs`.
	)");
	khalil2016.def(py::init<bool>(), py::arg("pseudo_candidates") = false, R"(
		Create new observation.

		Parameters
		----------
		pseudo_candidates:
				Whether the pseudo branching variable candidates (``SCIPgetPseudoBranchCands``)
				or LP branching variable candidates (``SCIPgetPseudoBranchCands``) are observed.
	)");
	def_before_reset(khalil2016, R"(Reset static features cache.)");
	def_extract(khalil2016, "Extract the observation matrix.");

	// Hutter2011 observation
	auto hutter_obs = ecole::python::auto_class<Hutter2011Obs>(m, "Hutter2011Obs", R"(
		Instance features from Hutter et al. (2011).

		The observation is a vector of features that globally characterize the instance.
		See [Hutter2011]_ for a complete reference on this observation function.

		.. [Hutter2011]
			Hutter, Frank, Hoos, Holger H., and Leyton-Brown, Kevin.
			"`Sequential model-based optimization for general algorithm configuration.
			<https://doi.org/10.1007/978-3-642-25566-3_40>`_"
			*International Conference on Learning and Intelligent Optimization*. 2011.
	)");
	hutter_obs.def_auto_copy()
		.def_auto_pickle("features")
		.def_readwrite_xtensor("features", &Hutter2011Obs::features, "A vector of instance features.");

	py::enum_<Hutter2011Obs::Features>(hutter_obs, "Features")
		.value("nb_variables", Hutter2011Obs::Features::nb_variables)
		.value("nb_constraints", Hutter2011Obs::Features::nb_constraints)
		.value("nb_nonzero_coefs", Hutter2011Obs::Features::nb_nonzero_coefs)
		.value("variable_node_degree_mean", Hutter2011Obs::Features::variable_node_degree_mean)
		.value("variable_node_degree_max", Hutter2011Obs::Features::variable_node_degree_max)
		.value("variable_node_degree_min", Hutter2011Obs::Features::variable_node_degree_min)
		.value("variable_node_degree_std", Hutter2011Obs::Features::variable_node_degree_std)
		.value("constraint_node_degree_mean", Hutter2011Obs::Features::constraint_node_degree_mean)
		.value("constraint_node_degree_max", Hutter2011Obs::Features::constraint_node_degree_max)
		.value("constraint_node_degree_min", Hutter2011Obs::Features::constraint_node_degree_min)
		.value("constraint_node_degree_std", Hutter2011Obs::Features::constraint_node_degree_std)
		.value("node_degree_mean", Hutter2011Obs::Features::node_degree_mean)
		.value("node_degree_max", Hutter2011Obs::Features::node_degree_max)
		.value("node_degree_min", Hutter2011Obs::Features::node_degree_min)
		.value("node_degree_std", Hutter2011Obs::Features::node_degree_std)
		.value("node_degree_25q", Hutter2011Obs::Features::node_degree_25q)
		.value("node_degree_75q", Hutter2011Obs::Features::node_degree_75q)
		.value("edge_density", Hutter2011Obs::Features::edge_density)
		.value("lp_slack_mean", Hutter2011Obs::Features::lp_slack_mean)
		.value("lp_slack_max", Hutter2011Obs::Features::lp_slack_max)
		.value("lp_slack_l2", Hutter2011Obs::Features::lp_slack_l2)
		.value("lp_objective_value", Hutter2011Obs::Features::lp_objective_value)
		.value("objective_coef_m_std", Hutter2011Obs::Features::objective_coef_m_std)
		.value("objective_coef_n_std", Hutter2011Obs::Features::objective_coef_n_std)
		.value("objective_coef_sqrtn_std", Hutter2011Obs::Features::objective_coef_sqrtn_std)
		.value("constraint_coef_mean", Hutter2011Obs::Features::constraint_coef_mean)
		.value("constraint_coef_std", Hutter2011Obs::Features::constraint_coef_std)
		.value("constraint_var_coef_mean", Hutter2011Obs::Features::constraint_var_coef_mean)
		.value("constraint_var_coef_std", Hutter2011Obs::Features::constraint_var_coef_std)
		.value("discrete_vars_support_size_mean", Hutter2011Obs::Features::discrete_vars_support_size_mean)
		.value("discrete_vars_support_size_std", Hutter2011Obs::Features::discrete_vars_support_size_std)
		.value("ratio_unbounded_discrete_vars", Hutter2011Obs::Features::ratio_unbounded_discrete_vars)
		.value("ratio_continuous_vars", Hutter2011Obs::Features::ratio_continuous_vars);

	auto hutter = py::class_<Hutter2011>(m, "Hutter2011", R"(
		Instance features from Hutter et al. (2011).

		This observation function extracts a structured :py:class:`Hutter2011Obs`.
	)");
	hutter.def(py::init<>());
	def_before_reset(hutter, R"(Do nothing.)");
	def_extract(hutter, "Extract the observation matrix.");

	// Focus node observation
	py::class_<FocusNodeObs>(m, "FocusNodeObs", R"(
        Focus node observation.
    )")  //
		.def_property_readonly(
			"number", [](FocusNodeObs & self) -> auto& { return self.number; }, "Add description.")
		.def_property_readonly(
			"depth", [](FocusNodeObs & self) -> auto& { return self.depth; }, "Add description.")
		.def_property_readonly(
			"lowerbound", [](FocusNodeObs & self) -> auto& { return self.lowerbound; }, "Add description.")
		.def_property_readonly(
			"estimate", [](FocusNodeObs & self) -> auto& { return self.estimate; }, "Add description.")
		.def_property_readonly(
			"n_added_conss", [](FocusNodeObs & self) -> auto& { return self.n_added_conss; }, "Add description.")
		.def_property_readonly(
			"n_vars", [](FocusNodeObs & self) -> auto& { return self.n_vars; }, "Add description.")
		.def_property_readonly(
			"nlpcands", [](FocusNodeObs & self) -> auto& { return self.nlpcands; }, "Add description.")
		.def_property_readonly(
			"npseudocands", [](FocusNodeObs & self) -> auto& { return self.npseudocands; }, "Add description.")
		.def_property_readonly(
			"parent_number", [](FocusNodeObs & self) -> auto& { return self.parent_number; }, "Add description.")
		.def_property_readonly(
			"parent_lowerbound", [](FocusNodeObs & self) -> auto& { return self.parent_lowerbound; }, "Add description.");

	auto focus_node = py::class_<FocusNode>(m, "FocusNode", R"(
        Returns data of the current node (focus node).
    )");
	focus_node.def(py::init<>());
	def_before_reset(focus_node, R"(Do nothing.)");
	def_extract(focus_node, "Extract a new :py:class:`FocusNodeObs`.");

	// Capacity observation
	auto capacity = py::class_<Capacity>(m, "Capacity", R"(
        Returns capacity of knapsacks per variable.
    )");
	capacity.def(py::init<>());
	def_before_reset(capacity, R"(Do nothing.)");
	def_extract(capacity, "Extract capacity");

	// Weight observation
	auto weight = py::class_<Weight>(m, "Weight", R"(
        Returns weight of the item per variable.
    )");
	weight.def(py::init<>());
	def_before_reset(weight, R"(Do nothing.)");
	def_extract(weight, "Extract weight");
}

}  // namespace ecole::observation
