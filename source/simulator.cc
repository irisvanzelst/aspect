/* $Id: step-32.cc 234 2011-10-19 18:07:35Z bangerth $ */
/* Author: Martin Kronbichler, Uppsala University,
           Wolfgang Bangerth, Texas A&M University,
     Timo Heister, University of Goettingen, 2008-2011 */
/*                                                                */
/*    Copyright (C) 2008, 2009, 2010, 2011 by the deal.II authors */
/*                                                                */
/*    This file is subject to QPL and may not be  distributed     */
/*    without copyright and license information. Please refer     */
/*    to the file deal.II/doc/license.html for the  text  and     */
/*    further information on this license.                        */

#include <aspect/simulator.h>
#include <aspect/equation_data.h>
#include <aspect/global.h>
#include <aspect/postprocess_visualization.h>
#include <aspect/material_model_base.h>
#include <aspect/adiabatic_conditions.h>

#include <deal.II/base/quadrature_lib.h>
#include <deal.II/base/logstream.h>
#include <deal.II/base/function.h>
#include <deal.II/base/utilities.h>
#include <deal.II/base/conditional_ostream.h>
#include <deal.II/base/work_stream.h>
#include <deal.II/base/timer.h>
#include <deal.II/base/parameter_handler.h>

#include <deal.II/lac/full_matrix.h>
#include <deal.II/lac/solver_bicgstab.h>
#include <deal.II/lac/solver_cg.h>
#include <deal.II/lac/solver_gmres.h>
#include <deal.II/lac/constraint_matrix.h>
#include <deal.II/lac/block_sparsity_pattern.h>

#include <deal.II/grid/tria.h>
#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/tria_accessor.h>
#include <deal.II/grid/tria_iterator.h>
#include <deal.II/grid/filtered_iterator.h>
#include <deal.II/grid/grid_tools.h>
#include <deal.II/grid/grid_refinement.h>

#include <deal.II/dofs/dof_renumbering.h>
#include <deal.II/dofs/dof_accessor.h>
#include <deal.II/dofs/dof_tools.h>

#include <deal.II/fe/fe_q.h>
#include <deal.II/fe/fe_dgq.h>
#include <deal.II/fe/fe_dgp.h>
#include <deal.II/fe/fe_values.h>

#include <deal.II/numerics/vectors.h>
#include <deal.II/numerics/matrices.h>
#include <deal.II/numerics/error_estimator.h>
#include <deal.II/numerics/solution_transfer.h>

#include <deal.II/base/index_set.h>

#include <deal.II/distributed/tria.h>
#include <deal.II/distributed/solution_transfer.h>
#include <deal.II/distributed/grid_refinement.h>

#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <limits>
#include <locale>
#include <string>


using namespace dealii;

// In the following namespace, we define the
// various pieces of equation data. All of
// these are exhaustively discussed in the
// description of the testcase in the
// introduction:
namespace EquationData
{
  double kappa                 = 1e-6;

  double T0      = 4000+273;              /* K          */
  double T1      =  700+273;              /* K          */

  struct Perturbation
  {
    double Angle;
    double depth;
    double Amplitude;
    double Sigma;
    double Sign;
    bool GaussianPerturbation;
  };

  Perturbation perturbation;






  template <int dim>
  class TemperatureInitialValues : public Function<dim>
  {
    public:
      TemperatureInitialValues () : Function<dim>(1) {}

      virtual double value (const Point<dim>   &p,
                            const unsigned int  component = 0) const;

      virtual void vector_value (const Point<dim> &p,
                                 Vector<double>   &value) const;
  };



  template <int dim>
  double
  TemperatureInitialValues<dim>::value (const Point<dim>  &p,
                                        const unsigned int) const
  {
    const double r = p.norm();
    //TODO: do something more reasonable here: query the geometry description
    const double R0 = 5698e3;
    const double R1 = 10415e3;
    const double h = R1-R0;

    // s = fraction of the way from
    // the inner to the outer
    // boundary; 0<=s<=1
    const double s = (r-R0)/h;
    double Perturbation = 0e0;
    double InterpolVal = 0e0;
    double depth[4];
    double geotherm[4];
    double x, y;
    double scale=R1/(R1 - R0);
    float eps = 1e-4;
    if (!EquationData::perturbation.GaussianPerturbation)
      {

        /* now compute an angular variation of the linear temperature field by
           stretching the variable s appropriately. note that the following
           formula leaves the end points s=0 and s=1 fixed, but stretches the
           region in between depending on the angle phi=atan2(x,y).

           For a plot, see
           http://www.wolframalpha.com/input/?i=plot+%28%282*sqrt%28x^2%2By^2%29-1%29%2B0.2*%282*sqrt%28x^2%2By^2%29-1%29*%281-%282*sqrt%28x^2%2By^2%29-1%29%29*sin%286*atan2%28x%2Cy%29%29%29%2C+x%3D-1+to+1%2C+y%3D-1+to+1
        */
        const double scale = (dim==3)?std::max(0.0,cos(3.14159*abs(p(2)/R1))):1.0;
        const double phi   = std::atan2(p(0),p(1));
        const double s_mod = s
                             +
                             0.2 * s * (1-s) * std::sin(6*phi) * scale;

        return T0*(1.0-s_mod) + T1*s_mod;
      }
    else
      {
        geotherm[3]=T1;
        geotherm[2]=T1 + 1200e0;
        geotherm[1]=T0 - 1300e0;
        geotherm[0]=T0;
        depth[0]=R0-1e-2*R0;
        depth[1]=R0+500e3;
        depth[2]=R1-500e3;
        depth[3]=R1+1e-2*R1;

        int indx = -1;
        for (unsigned int i=0; i<3; ++i)
          {
            if ((depth[i] - r) < eps && (depth[i+1] - r) > eps)
              {
                indx = i;
                break;
              }
          }
        Assert (indx >= 0,                  ExcInternalError());
        Assert (indx < 3,                  ExcInternalError());
        int indx1 = indx + 1;
        float dx = depth[indx1] - depth[indx];
        float dy = geotherm[indx1] - geotherm[indx];

        if ( dx > 0.5*eps)
          {
            // linear interpolation
            InterpolVal = std::max(geotherm[3],geotherm[indx] + (r-depth[indx]) * (dy/dx));
          }
        else
          {
            // eval.point in discontinuity
            InterpolVal = 0.5*( geotherm[indx] + geotherm[indx1] );
          }
        x = (scale - EquationData::perturbation.depth)*std::cos(EquationData::perturbation.Angle);
        y = (scale - EquationData::perturbation.depth)*std::sin(EquationData::perturbation.Angle);
        Perturbation = EquationData::perturbation.Sign*EquationData::perturbation.Amplitude*T0*std::exp( -( std::pow((p(0)*scale/R1-x),2) +std::pow((p(1)*scale/R1-y),2) ) / EquationData::perturbation.Sigma) ;
        if (r > R1 - 1e-2*R1)
          {
            return geotherm[3];
          }
        else if (r < R0 + 1e-2*R0)
          {
            return geotherm[0];
          }
        else
          {
            return InterpolVal + Perturbation;
          }
      }
  }



  template <int dim>
  void
  TemperatureInitialValues<dim>::vector_value (const Point<dim> &p,
                                               Vector<double>   &values) const
  {
    for (unsigned int c=0; c<this->n_components; ++c)
      values(c) = TemperatureInitialValues<dim>::value (p, c);
  }
}



namespace aspect
{

  template <int dim>
  Simulator<dim>::Parameters::Parameters (ParameterHandler &prm)
  {
    parse_parameters (prm);
  }


  template <int dim>
  void
  Simulator<dim>::Parameters::
  declare_parameters (ParameterHandler &prm)
  {
    prm.declare_entry ("Resume computation", "false",
                       Patterns::Bool (),
                       "should the last computation be resumed?");

    prm.declare_entry ("End time", "1e8",
                       Patterns::Double (0),
                       "The end time of the simulation in years.");

    prm.declare_entry ("Time step scaling", "1.0",
                       Patterns::Double (0),
                       "Time step size is given as scaling/cfl_number, default: 1.0");

    prm.enter_subsection ("Mesh refinement");
    {
      prm.declare_entry ("Initial global refinement", "2",
                         Patterns::Integer (0),
                         "The number of global refinement steps performed on "
                         "the initial coarse mesh, before the problem is first "
                         "solved there.");
      prm.declare_entry ("Initial adaptive refinement", "2",
                         Patterns::Integer (0),
                         "The number of adaptive refinement steps performed after "
                         "initial global refinement.");
      prm.declare_entry ("Time steps between mesh refinement", "10",
                         Patterns::Integer (1),
                         "The number of time steps after which the mesh is to be "
                         "adapted based on computed error indicators.");
      prm.declare_entry ("Refinement fraction", "0.3",
                         Patterns::Double(0,1),
                         "The fraction of cells with the largest error that "
                         "should be flagged for refinement.");
      prm.declare_entry ("Coarsening fraction", "0.05",
                         Patterns::Double(0,1),
                         "The fraction of cells with the smallest error that "
                         "should be flagged for coarsening.");
      prm.declare_entry ("Additional refinement times", "",
                         Patterns::List (Patterns::Double(0)),
                         "A list of times (in years) so that if the end time of a time step "
                         "is beyond this time, an additional round of mesh refinement "
                         "is triggered. This is mostly useful to make sure we "
                         "can get through the initial transient phase of a simulation "
                         "on a relatively coarse mesh, and then refine again when we "
                         "are in a time range that we are interested in and where "
                         "we would like to use a finer mesh.");
    }
    prm.leave_subsection();

    prm.enter_subsection ("Stabilization parameters");
    {
      prm.declare_entry ("alpha", "2",
                         Patterns::Double (1, 2),
                         "The exponent in the entropy viscosity stabilization.");
      prm.declare_entry ("c_R", "0.11",
                         Patterns::Double (0),
                         "The c_R factor in the entropy viscosity "
                         "stabilization.");
      prm.declare_entry ("beta", "0.078",
                         Patterns::Double (0),
                         "The beta factor in the artificial viscosity "
                         "stabilization. An appropriate value for 2d is 0.052 "
                         "and 0.078 for 3d.");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Thermal perturbation");
    {
      prm.declare_entry ("Angle", "0e0",
                         Patterns::Double (0),
                         "The angle where the center of the perturbation is placed.");
      prm.declare_entry ("non-dim depth", "0.7",
                         Patterns::Double (0),
                         "The radial distance where the center of the perturbation is placed.");
      prm.declare_entry ("Amplitude", "0.01",
                         Patterns::Double (0),
                         "The amplitude of the perturbation.");
      prm.declare_entry ("Sigma", "0.2",
                         Patterns::Double (0),
                         "The standard deviation of the Gaussian perturbation.");
      prm.declare_entry ("Sign", "1",
                         Patterns::Double (),
                         "The sign of the perturbation.");
      prm.declare_entry ("gaussian perturbation", "true",
                         Patterns::Bool (),
                         "The sign of the perturbation.");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("ModelSettings");
    {
      prm.declare_entry ("Include shear heating", "true",
                         Patterns::Bool (),
                         "Whether to include shear heating into the model or not. From a "
                         "physical viewpoint, shear heating should always be used but may "
                         "be undesirable when comparing results with known benchmarks that "
                         "do not include this term in the temperature equation.");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("ModelParameters");
    {
      prm.declare_entry ("kappa", "4.548e-7",
                         Patterns::Double (),
                         "thermal diffusivity (k/(rho*cp)");
      prm.declare_entry ("Radiogenic heating rate", "0e0",
                         Patterns::Double (),
                         "H0");
      prm.declare_entry ("T1", "0",
                         Patterns::Double (),
                         "temperature at outer boundary (lythosphere water/air)");
      prm.declare_entry ("T0", "6000",
                         Patterns::Double (),
                         "temperature at inner boundary (core mantle boundary");
    }
    prm.leave_subsection ();


    prm.enter_subsection ("Discretization");
    {
      prm.declare_entry ("Stokes velocity polynomial degree", "2",
                         Patterns::Integer (1),
                         "The polynomial degree to use for the velocity variables "
                         "in the Stokes system.");
      prm.declare_entry ("Temperature polynomial degree", "2",
                         Patterns::Integer (1),
                         "The polynomial degree to use for the temperature variable.");
      prm.declare_entry ("Use locally conservative discretization", "true",
                         Patterns::Bool (),
                         "Whether to use a Stokes discretization that is locally "
                         "conservative at the expense of a larger number of degrees "
                         "of freedom, or to go with a cheaper discretization "
                         "that does not locally conserve mass (although it is "
                         "globally conservative.");
    }
    prm.leave_subsection ();
  }



  template <int dim>
  void
  Simulator<dim>::Parameters::
  parse_parameters (ParameterHandler &prm)
  {
    resume_computation      = prm.get_bool ("Resume computation");
    end_time                = prm.get_double ("End time");
    time_step_scaling       = prm.get_double ("Time step scaling");

    prm.enter_subsection ("Mesh refinement");
    {
      initial_global_refinement   = prm.get_integer ("Initial global refinement");
      initial_adaptive_refinement = prm.get_integer ("Initial adaptive refinement");

      adaptive_refinement_interval= prm.get_integer ("Time steps between mesh refinement");
      refinement_fraction         = prm.get_double ("Refinement fraction");
      coarsening_fraction         = prm.get_double ("Coarsening fraction");

      // extract the list of times at which additional refinement is requested
      // then sort it and convert it to seconds
      additional_refinement_times
        = Utilities::string_to_double
          (Utilities::split_string_list(prm.get ("Additional refinement times")));
      std::sort (additional_refinement_times.begin(),
                 additional_refinement_times.end());
      for (unsigned int i=0; i<additional_refinement_times.size(); ++i)
        additional_refinement_times[i] *= year_in_seconds;
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Stabilization parameters");
    {
      stabilization_alpha = prm.get_double ("alpha");
      stabilization_c_R   = prm.get_double ("c_R");
      stabilization_beta  = prm.get_double ("beta");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("Thermal perturbation");
    {
      EquationData::perturbation.Angle = prm.get_double ("Angle");
      EquationData::perturbation.depth = prm.get_double ("non-dim depth");
      //EquationData::perturbation.depth = EquationData::R1 - (EquationData::R1 - EquationData::R0)*prm.get_double ("non-dim depth");
      EquationData::perturbation.Amplitude  = prm.get_double ("Amplitude");
      EquationData::perturbation.Sigma  = prm.get_double ("Sigma");
      EquationData::perturbation.Sign  = prm.get_double ("Sign");
      EquationData::perturbation.GaussianPerturbation  = prm.get_bool ("gaussian perturbation");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("ModelSettings");
    {
      include_shear_heating = prm.get_bool ("Include shear heating");
    }
    prm.leave_subsection ();

    prm.enter_subsection ("ModelParameters");
    {
      EquationData::kappa = prm.get_double ("kappa");
      radiogenic_heating_rate = prm.get_double ("Radiogenic heating rate");
      EquationData::T0 = prm.get_double ("T0");
      EquationData::T1 = prm.get_double ("T1");
    }

    prm.leave_subsection ();

    prm.enter_subsection ("Discretization");
    {
      stokes_velocity_degree = prm.get_integer ("Stokes velocity polynomial degree");
      temperature_degree     = prm.get_integer ("Temperature polynomial degree");
      use_locally_conservative_discretization
        = prm.get_bool ("Use locally conservative discretization");
    }
    prm.leave_subsection ();
  }



  template <int dim>
  Simulator<dim>::Simulator (ParameterHandler &prm)
    :
    parameters (prm),
    pcout (std::cout,
           (Utilities::MPI::
            this_mpi_process(MPI_COMM_WORLD)
            == 0)),

    geometry_model (GeometryModel::create_geometry_model<dim>(prm)),
    material_model (MaterialModel::create_material_model<dim>(prm)),
    gravity_model (GravityModel::create_gravity_model<dim>(prm)),

    triangulation (MPI_COMM_WORLD,
                   typename Triangulation<dim>::MeshSmoothing
                   (Triangulation<dim>::smoothing_on_refinement |
                    Triangulation<dim>::smoothing_on_coarsening),
                   parallel::distributed::Triangulation<dim>::mesh_reconstruction_after_repartitioning),

    mapping (4),

    stokes_fe (FE_Q<dim>(parameters.stokes_velocity_degree),
               dim,
               (parameters.use_locally_conservative_discretization
                ?
                static_cast<const FiniteElement<dim> &>
                (FE_DGP<dim>(parameters.stokes_velocity_degree-1))
                :
                static_cast<const FiniteElement<dim> &>
                (FE_Q<dim>(parameters.stokes_velocity_degree-1))),
               1),

    stokes_dof_handler (triangulation),

    temperature_fe (parameters.temperature_degree),
    temperature_dof_handler (triangulation),

    time (0),
    time_step (0),
    old_time_step (0),
    timestep_number (0),
    rebuild_stokes_matrix (true),
    rebuild_stokes_preconditioner (true),

    computing_timer (pcout, TimerOutput::summary,
                     TimerOutput::wall_times)
  {
    postprocess_manager.parse_parameters (prm);
    postprocess_manager.initialize (*this);

    geometry_model->create_coarse_mesh (triangulation);
    global_Omega_diameter = GridTools::diameter (triangulation);

    adiabatic_conditions.reset (new AdiabaticConditions<dim>(*geometry_model,
                                                             *gravity_model,
                                                             *material_model));

    pressure_scaling = material_model->reference_viscosity() / geometry_model->length_scale();

    // make sure that we don't have to fill every column of the statistics
    // object in each time step.
    statistics.set_auto_fill_mode(true);
  }



  template <int dim>
  void Simulator<dim>::declare_parameters (ParameterHandler &prm)
  {
    Parameters::declare_parameters (prm);
    Postprocess::Manager<dim>::declare_parameters (prm);
    MaterialModel::declare_parameters (prm);
    GeometryModel::declare_parameters (prm);
    GravityModel::declare_parameters (prm);
  }



  template <int dim>
  double Simulator<dim>::get_maximal_velocity () const
  {
    const QIterated<dim> quadrature_formula (QTrapez<1>(),
                                             parameters.stokes_velocity_degree);
    const unsigned int n_q_points = quadrature_formula.size();

    FEValues<dim> fe_values (mapping, stokes_fe, quadrature_formula, update_values);
    std::vector<Tensor<1,dim> > velocity_values(n_q_points);

    const FEValuesExtractors::Vector velocities (0);

    double max_local_velocity = 0;

    typename DoFHandler<dim>::active_cell_iterator
    cell = stokes_dof_handler.begin_active(),
    endc = stokes_dof_handler.end();
    for (; cell!=endc; ++cell)
      if (cell->is_locally_owned())
        {
          fe_values.reinit (cell);
          fe_values[velocities].get_function_values (stokes_solution,
                                                     velocity_values);

          for (unsigned int q=0; q<n_q_points; ++q)
            max_local_velocity = std::max (max_local_velocity,
                                           velocity_values[q].norm());
        }

    return Utilities::MPI::max (max_local_velocity, MPI_COMM_WORLD);
  }



  template <int dim>
  double
  Simulator<dim>::get_entropy_variation (const double average_temperature) const
  {
    // only do this if we really need entropy
    // variation
    if (parameters.stabilization_alpha != 2)
      return 1.;

    // record maximal entropy on Gauss quadrature
    // points
    const QGauss<dim> quadrature_formula (parameters.temperature_degree+1);
    const unsigned int n_q_points = quadrature_formula.size();

    FEValues<dim> fe_values (temperature_fe, quadrature_formula,
                             update_values | update_JxW_values);
    std::vector<double> old_temperature_values(n_q_points);
    std::vector<double> old_old_temperature_values(n_q_points);

    double min_entropy = std::numeric_limits<double>::max(),
           max_entropy = -std::numeric_limits<double>::max(),
           area = 0,
           entropy_integrated = 0;

    typename DoFHandler<dim>::active_cell_iterator
    cell = temperature_dof_handler.begin_active(),
    endc = temperature_dof_handler.end();
    for (; cell!=endc; ++cell)
      if (cell->is_locally_owned())
        {
          fe_values.reinit (cell);
          fe_values.get_function_values (old_temperature_solution,
                                         old_temperature_values);
          fe_values.get_function_values (old_old_temperature_solution,
                                         old_old_temperature_values);
          for (unsigned int q=0; q<n_q_points; ++q)
            {
              const double T = (old_temperature_values[q] +
                                old_old_temperature_values[q]) / 2;
              const double entropy = ((T-average_temperature) *
                                      (T-average_temperature));

              min_entropy = std::min (min_entropy, entropy);
              max_entropy = std::max (max_entropy, entropy);
              area += fe_values.JxW(q);
              entropy_integrated += fe_values.JxW(q) * entropy;
            }
        }

    // do MPI data exchange: we need to sum over
    // the two integrals (area,
    // entropy_integrated), and get the extrema
    // for maximum and minimum. combine
    // MPI_Allreduce for two values since that is
    // an expensive operation
    const double local_for_sum[2] = { entropy_integrated, area },
                                    local_for_max[2] = { -min_entropy, max_entropy };
    double global_for_sum[2], global_for_max[2];

    Utilities::MPI::sum (local_for_sum, MPI_COMM_WORLD, global_for_sum);
    Utilities::MPI::max (local_for_max, MPI_COMM_WORLD, global_for_max);

    const double average_entropy = global_for_sum[0] / global_for_sum[1];
    const double entropy_diff = std::max(global_for_max[1] - average_entropy,
                                         average_entropy - (-global_for_max[0]));
    return entropy_diff;
  }



  // Similar function to before, but we now
  // compute the cfl number, i.e., maximal
  // velocity on a cell divided by the cell
  // diameter
  template <int dim>
  double Simulator<dim>::get_cfl_number () const
  {
    const QIterated<dim> quadrature_formula (QTrapez<1>(),
                                             parameters.stokes_velocity_degree);
    const unsigned int n_q_points = quadrature_formula.size();

    FEValues<dim> fe_values (mapping, stokes_fe, quadrature_formula, update_values);
    std::vector<Tensor<1,dim> > velocity_values(n_q_points);

    const FEValuesExtractors::Vector velocities (0);

    double max_local_cfl = 0;

    typename DoFHandler<dim>::active_cell_iterator
    cell = stokes_dof_handler.begin_active(),
    endc = stokes_dof_handler.end();
    for (; cell!=endc; ++cell)
      if (cell->is_locally_owned())
        {
          fe_values.reinit (cell);
          fe_values[velocities].get_function_values (stokes_solution,
                                                     velocity_values);

          double max_local_velocity = 1e-10;
          for (unsigned int q=0; q<n_q_points; ++q)
            max_local_velocity = std::max (max_local_velocity,
                                           velocity_values[q].norm());
          max_local_cfl = std::max(max_local_cfl,
                                   max_local_velocity
                                   /
                                   cell->minimum_vertex_distance());
        }

    return Utilities::MPI::max (max_local_cfl, MPI_COMM_WORLD);
  }



  template <int dim>
  std::pair<double,double>
  Simulator<dim>::get_extrapolated_temperature_range () const
  {
    const QIterated<dim> quadrature_formula (QTrapez<1>(),
                                             parameters.temperature_degree);
    const unsigned int n_q_points = quadrature_formula.size();

    FEValues<dim> fe_values (mapping, temperature_fe, quadrature_formula,
                             update_values);
    std::vector<double> old_temperature_values(n_q_points);
    std::vector<double> old_old_temperature_values(n_q_points);

    // This presets the minimum with a bigger
    // and the maximum with a smaller number
    // than one that is going to appear. Will
    // be overwritten in the cell loop or in
    // the communication step at the
    // latest.
    double min_local_temperature = std::numeric_limits<double>::max(),
           max_local_temperature = -std::numeric_limits<double>::max();

    if (timestep_number != 0)
      {
        typename DoFHandler<dim>::active_cell_iterator
        cell = temperature_dof_handler.begin_active(),
        endc = temperature_dof_handler.end();
        for (; cell!=endc; ++cell)
          if (cell->is_locally_owned())
            {
              fe_values.reinit (cell);
              fe_values.get_function_values (old_temperature_solution,
                                             old_temperature_values);
              fe_values.get_function_values (old_old_temperature_solution,
                                             old_old_temperature_values);

              for (unsigned int q=0; q<n_q_points; ++q)
                {
                  const double temperature =
                    (1. + time_step/old_time_step) * old_temperature_values[q]-
                    time_step/old_time_step * old_old_temperature_values[q];

                  min_local_temperature = std::min (min_local_temperature,
                                                    temperature);
                  max_local_temperature = std::max (max_local_temperature,
                                                    temperature);
                }
            }
      }
    else
      {
        typename DoFHandler<dim>::active_cell_iterator
        cell = temperature_dof_handler.begin_active(),
        endc = temperature_dof_handler.end();
        for (; cell!=endc; ++cell)
          if (cell->is_locally_owned())
            {
              fe_values.reinit (cell);
              fe_values.get_function_values (old_temperature_solution,
                                             old_temperature_values);

              for (unsigned int q=0; q<n_q_points; ++q)
                {
                  const double temperature = old_temperature_values[q];

                  min_local_temperature = std::min (min_local_temperature,
                                                    temperature);
                  max_local_temperature = std::max (max_local_temperature,
                                                    temperature);
                }
            }
      }

    return std::make_pair(-Utilities::MPI::max (-min_local_temperature,
                                                MPI_COMM_WORLD),
                          Utilities::MPI::max (max_local_temperature,
                                               MPI_COMM_WORLD));
  }




  template <int dim>
  void Simulator<dim>::set_initial_temperature_field ()
  {
    // create a fully distributed vector since
    // the VectorTools::interpolate function
    // needs to write into it and we can not
    // write into vectors with ghost elements
    TrilinosWrappers::MPI::Vector
    solution (temperature_matrix.row_partitioner());

    // interpolate the initial values
    VectorTools::interpolate (mapping,
                              temperature_dof_handler,
                              EquationData::TemperatureInitialValues<dim>(),
                              solution);

    // then apply constraints and copy the
    // result into vectors with ghost elements
    temperature_constraints.distribute (solution);
    temperature_solution = solution;
    old_temperature_solution = solution;
    old_old_temperature_solution = solution;
  }



  template <int dim>
  void Simulator<dim>::compute_initial_pressure_field ()
  {
    // we'd like to interpolate the initial pressure onto the pressure
    // variable but that's a bit involved because the pressure may either
    // be an FE_Q (for which we can interpolate) or an FE_DGP (for which
    // we can't since the element has no nodal basis.
    //
    // fortunately, in the latter case, the element is discontinuous and
    // we can compute a local projection onto the pressure space
    if (parameters.use_locally_conservative_discretization == false)
      {
        class InitialConditions : public Function<dim>
        {
          public:
            InitialConditions (const AdiabaticConditions<dim> &ad_c)
              : Function<dim> (dim+1), adiabatic_conditions (ad_c)
            {}

            double value (const Point<dim> &p,
                          const unsigned int component) const
            {
              switch (component)
                {
                  case dim:
                    return adiabatic_conditions.pressure (p);
                  default:
                    return 0;
                }
            }

            const AdiabaticConditions<dim> & adiabatic_conditions;
        };

        TrilinosWrappers::MPI::BlockVector stokes_tmp;
        stokes_tmp.reinit (stokes_rhs);
        VectorTools::interpolate (mapping, stokes_dof_handler,
                                  InitialConditions(*adiabatic_conditions),
                                  stokes_tmp);
        old_stokes_solution = stokes_tmp;
      }
    else
      {
        // implement the local projection for the discontinuous pressure
        // element. this is only going to work if, indeed, the element
        // is discontinuous
        const FiniteElement<dim> &pressure_fe = stokes_fe.base_element(1);
        Assert (pressure_fe.dofs_per_face == 0,
                ExcNotImplemented());

        QGauss<dim> quadrature(parameters.stokes_velocity_degree+1);
        UpdateFlags update_flags = UpdateFlags(update_values   |
                                               update_quadrature_points |
                                               update_JxW_values);
        FEValues<dim> fe_values (mapping, stokes_fe, quadrature, update_flags);
        const FEValuesExtractors::Scalar pressure (dim);

        const unsigned int
        dofs_per_cell = fe_values.dofs_per_cell,
        n_q_points    = fe_values.n_quadrature_points;

        std::vector<unsigned int> local_dof_indices (dofs_per_cell);
        Vector<double> cell_vector (dofs_per_cell);
        Vector<double> local_projection (dofs_per_cell);
        FullMatrix<double> local_mass_matrix (dofs_per_cell, dofs_per_cell);

        std::vector<double> rhs_values(n_q_points);

        ScalarFunctionFromFunctionObject<dim>
        ad_pressure (std_cxx1x::bind (&AdiabaticConditions<dim>::pressure,
                                      std_cxx1x::cref(*adiabatic_conditions),
                                      std_cxx1x::_1));


        typename DoFHandler<dim>::active_cell_iterator
        cell = stokes_dof_handler.begin_active(),
        endc = stokes_dof_handler.end();

        for (; cell!=endc; ++cell)
          if (cell->is_locally_owned())
            {
              cell->get_dof_indices (local_dof_indices);
              fe_values.reinit(cell);

              ad_pressure.value_list (fe_values.get_quadrature_points(),
                                      rhs_values);

              cell_vector = 0;
              local_mass_matrix = 0;
              for (unsigned int point=0; point<n_q_points; ++point)
                for (unsigned int i=0; i<dofs_per_cell; ++i)
                  {
                    if (stokes_fe.system_to_component_index(i).first == dim)
                      cell_vector(i)
                      +=
                        rhs_values[point] *
                        fe_values[pressure].value(i,point) *
                        fe_values.JxW(point);

                    // populate the local matrix; create the pressure mass matrix
                    // in the pressure pressure block and the identity matrix
                    // for all other variables so that the whole thing remains
                    // invertible
                    for (unsigned int j=0; j<dofs_per_cell; ++j)
                      if ((stokes_fe.system_to_component_index(i).first == dim)
                          &&
                          (stokes_fe.system_to_component_index(j).first == dim))
                        local_mass_matrix(j,i) += (fe_values[pressure].value(i,point) *
                                                   fe_values[pressure].value(j,point) *
                                                   fe_values.JxW(point));
                      else if (i == j)
                        local_mass_matrix(i,j) = 1;
                  }

              // now invert the local mass matrix and multiply it with the rhs
              local_mass_matrix.gauss_jordan();
              local_mass_matrix.vmult (local_projection, cell_vector);

              // then set the global solution vector to the values just computed
              cell->set_dof_values (local_projection, old_stokes_solution);
            }
      }

    // normalize the pressure in such a way that the surface pressure
    // equals a known and desired value
    normalize_pressure(old_stokes_solution);

    // set the current solution to the same value as the previous solution
    stokes_solution = old_stokes_solution;
  }


  template <int dim>
  void Simulator<dim>::
  setup_stokes_matrix (const std::vector<IndexSet> &stokes_partitioning)
  {
    stokes_matrix.clear ();

    TrilinosWrappers::BlockSparsityPattern sp (stokes_partitioning,
                                               MPI_COMM_WORLD);

    Table<2,DoFTools::Coupling> coupling (dim+1, dim+1);

    for (unsigned int c=0; c<dim+1; ++c)
      for (unsigned int d=0; d<dim+1; ++d)
        if (! ((c==dim) && (d==dim)))
          coupling[c][d] = DoFTools::always;
        else
          coupling[c][d] = DoFTools::none;

    DoFTools::make_sparsity_pattern (stokes_dof_handler,
                                     coupling, sp,
                                     stokes_constraints, false,
                                     Utilities::MPI::
                                     this_mpi_process(MPI_COMM_WORLD));
    sp.compress();

    stokes_matrix.reinit (sp);
  }



  template <int dim>
  void Simulator<dim>::
  setup_stokes_preconditioner (const std::vector<IndexSet> &stokes_partitioning)
  {
    Amg_preconditioner.reset ();
    Mp_preconditioner.reset ();

    stokes_preconditioner_matrix.clear ();

    TrilinosWrappers::BlockSparsityPattern sp (stokes_partitioning,
                                               MPI_COMM_WORLD);

    Table<2,DoFTools::Coupling> coupling (dim+1, dim+1);
    for (unsigned int c=0; c<dim+1; ++c)
      for (unsigned int d=0; d<dim+1; ++d)
        if (c == d)
          coupling[c][d] = DoFTools::always;
        else
          coupling[c][d] = DoFTools::none;

    DoFTools::make_sparsity_pattern (stokes_dof_handler,
                                     coupling, sp,
                                     stokes_constraints, false,
                                     Utilities::MPI::
                                     this_mpi_process(MPI_COMM_WORLD));
    sp.compress();

    stokes_preconditioner_matrix.reinit (sp);
  }


  template <int dim>
  void Simulator<dim>::
  setup_temperature_matrix (const IndexSet &temperature_partitioner)
  {
    T_preconditioner.reset ();
    temperature_matrix.clear ();

    TrilinosWrappers::SparsityPattern sp (temperature_partitioner,
                                          MPI_COMM_WORLD);
    DoFTools::make_sparsity_pattern (temperature_dof_handler, sp,
                                     temperature_constraints, false,
                                     Utilities::MPI::
                                     this_mpi_process(MPI_COMM_WORLD));
    sp.compress();

    temperature_matrix.reinit (sp);
  }



  template <int dim>
  void Simulator<dim>::setup_dofs ()
  {
    computing_timer.enter_section("Setup dof systems");

    stokes_dof_handler.distribute_dofs (stokes_fe);

    // Renumber the DoFs hierarchical so that we get the
    // same numbering if we resume the computation. This
    // is because the numbering depends on the order the
    // cells are created.
    DoFRenumbering::hierarchical (stokes_dof_handler);
    std::vector<unsigned int> stokes_sub_blocks (dim+1,0);
    stokes_sub_blocks[dim] = 1;
    DoFRenumbering::component_wise (stokes_dof_handler, stokes_sub_blocks);

    temperature_dof_handler.distribute_dofs (temperature_fe);

    DoFRenumbering::hierarchical (temperature_dof_handler);
    std::vector<unsigned int> stokes_dofs_per_block (2);
    DoFTools::count_dofs_per_block (stokes_dof_handler, stokes_dofs_per_block,
                                    stokes_sub_blocks);

    const unsigned int n_u = stokes_dofs_per_block[0],
                       n_p = stokes_dofs_per_block[1],
                       n_T = temperature_dof_handler.n_dofs();

    // print dof numbers with 1000s
    // separator since they are frequently
    // large
    std::locale s = pcout.get_stream().getloc();
    // Creating std::locale with an empty string causes problems
    // on some platforms, so catch the exception and ignore
    try
      {
        pcout.get_stream().imbue(std::locale(""));
      }
    catch (std::runtime_error e)
      {
        // If the locale doesn't work, just give up
      }
    pcout << "Number of active cells: "
          << triangulation.n_global_active_cells()
          << " (on "
          << triangulation.n_levels()
          << " levels)"
          << std::endl
          << "Number of degrees of freedom: "
          << n_u + n_p + n_T
          << " (" << n_u << '+' << n_p << '+'<< n_T <<')'
          << std::endl
          << std::endl;
    pcout.get_stream().imbue(s);



    std::vector<IndexSet> stokes_partitioning, stokes_relevant_partitioning;
    IndexSet temperature_partitioning (n_T), temperature_relevant_partitioning (n_T);
    IndexSet stokes_relevant_set;
    {
      IndexSet stokes_index_set = stokes_dof_handler.locally_owned_dofs();
      stokes_partitioning.push_back(stokes_index_set.get_view(0,n_u));
      stokes_partitioning.push_back(stokes_index_set.get_view(n_u,n_u+n_p));

      DoFTools::extract_locally_relevant_dofs (stokes_dof_handler,
                                               stokes_relevant_set);
      stokes_relevant_partitioning.push_back(stokes_relevant_set.get_view(0,n_u));
      stokes_relevant_partitioning.push_back(stokes_relevant_set.get_view(n_u,n_u+n_p));

      temperature_partitioning = temperature_dof_handler.locally_owned_dofs();
      DoFTools::extract_locally_relevant_dofs (temperature_dof_handler,
                                               temperature_relevant_partitioning);
    }

    {

      stokes_constraints.clear ();
//    IndexSet stokes_la;
//    DoFTools::extract_locally_active_dofs (stokes_dof_handler,
//             stokes_la);
      stokes_constraints.reinit (stokes_relevant_set);

      DoFTools::make_hanging_node_constraints (stokes_dof_handler,
                                               stokes_constraints);

      /*    std::vector<bool> velocity_mask (dim+1, true);
      velocity_mask[dim] = false;
      VectorTools::interpolate_boundary_values (stokes_dof_handler,
                  0,
                  ZeroFunction<dim>(dim+1),
                  stokes_constraints,
                  velocity_mask);
      */
      std::set<unsigned char> no_normal_flux_boundaries;
      no_normal_flux_boundaries.insert (0);
      no_normal_flux_boundaries.insert (1);
      no_normal_flux_boundaries.insert (2);
      no_normal_flux_boundaries.insert (3);
      VectorTools::compute_no_normal_flux_constraints (stokes_dof_handler, 0,
                                                       no_normal_flux_boundaries,
                                                       stokes_constraints,
                                                       mapping);
      stokes_constraints.close ();
    }
    {
      temperature_constraints.clear ();
      temperature_constraints.reinit (temperature_relevant_partitioning);//temp_locally_active);

      DoFTools::make_hanging_node_constraints (temperature_dof_handler,
                                               temperature_constraints);
      VectorTools::interpolate_boundary_values (temperature_dof_handler,
                                                0,
                                                EquationData::TemperatureInitialValues<dim>(),
                                                temperature_constraints);
      VectorTools::interpolate_boundary_values (temperature_dof_handler,
                                                1,
                                                EquationData::TemperatureInitialValues<dim>(),
                                                temperature_constraints);
      temperature_constraints.close ();
    }

    setup_stokes_matrix (stokes_partitioning);
    setup_stokes_preconditioner (stokes_partitioning);
    setup_temperature_matrix (temperature_partitioning);

    stokes_rhs.reinit (stokes_partitioning, MPI_COMM_WORLD);
    stokes_rhs_helper.reinit (stokes_partitioning, MPI_COMM_WORLD);
    stokes_solution.reinit (stokes_relevant_partitioning, MPI_COMM_WORLD);
    old_stokes_solution.reinit (stokes_solution);

    temperature_rhs.reinit (temperature_partitioning, MPI_COMM_WORLD);
    temperature_solution.reinit (temperature_relevant_partitioning, MPI_COMM_WORLD);
    old_temperature_solution.reinit (temperature_solution);
    old_old_temperature_solution.reinit (temperature_solution);

    rebuild_stokes_matrix              = true;
    rebuild_stokes_preconditioner      = true;

    computing_timer.exit_section();
  }




  /*
   * normalize the pressure by calculating the surface integral of the pressure on the outer
   * shell and subtracting this from all pressure nodes.
   */
  template <int dim>
  void Simulator<dim>::normalize_pressure(TrilinosWrappers::MPI::BlockVector &vector)
  {
    double my_pressure = 0.0;
    double my_area = 0.0;
    {
      QGauss < dim - 1 > quadrature (parameters.stokes_velocity_degree + 1);

      const unsigned int n_q_points = quadrature.size();
      FEFaceValues<dim> fe_face_values (mapping, stokes_fe,  quadrature,
                                        update_JxW_values | update_values);
      const FEValuesExtractors::Scalar pressure (dim);

      std::vector<double> pressure_values(n_q_points);

      typename DoFHandler<dim>::active_cell_iterator
      cell = stokes_dof_handler.begin_active(),
      endc = stokes_dof_handler.end();
      for (; cell != endc; ++cell)
        if (cell->is_locally_owned())
          {
            for (unsigned int face_no = 0; face_no < GeometryInfo<dim>::faces_per_cell; ++face_no)
              {
                const typename DoFHandler<dim>::face_iterator face = cell->face (face_no);
                if (face->at_boundary() && face->boundary_indicator() == 1) // outer shell boundary
                  {
                    fe_face_values.reinit (cell, face_no);
                    fe_face_values[pressure].get_function_values(vector,
                                                                 pressure_values);

                    for (unsigned int q = 0; q < n_q_points; ++q)
                      {
                        my_pressure += pressure_values[q]
                                       * fe_face_values.JxW (q);
                        my_area += fe_face_values.JxW (q);
                      }
                  }
              }
          }
    }

    double surf_pressure = 0;
    // sum up the surface integrals from each processor
    {
      const double my_temp[2] = {my_pressure, my_area};
      double temp[2];
      Utilities::MPI::sum (my_temp, MPI_COMM_WORLD, temp);
      surf_pressure = temp[0]/temp[1];
    }

    const double adjust = -surf_pressure + 1e7;
    if (parameters.use_locally_conservative_discretization == false)
      vector.block(1).add(adjust);
    else
      {
        // this case is a bit more complicated: if the condition above is false
        // then we use the FE_DGP element for which the shape functions do not
        // add up to one; consequently, adding a constant to all degrees of
        // freedom does not alter the overall function by that constant, but
        // by something different
        //
        // we can work around this by using the documented property of the
        // FE_DGP element that the first shape function is constant.
        // consequently, adding the adjustment to the global function is
        // achieved by adding the adjustment to the first pressure degree
        // of freedom on each cell.
        Assert (dynamic_cast<const FE_DGP<dim>*>(&stokes_fe.base_element(1)) != 0,
                ExcInternalError());
        std::vector<unsigned int> local_dof_indices (stokes_fe.dofs_per_cell);
        typename DoFHandler<dim>::active_cell_iterator
        cell = stokes_dof_handler.begin_active(),
        endc = stokes_dof_handler.end();
        for (; cell != endc; ++cell)
          if (cell->is_locally_owned())
            {
              // identify the first pressure dof
              cell->get_dof_indices (local_dof_indices);
              const unsigned int first_pressure_dof
                = stokes_fe.component_to_system_index (dim, 0);

              // make sure that this DoF is really owned by the current processor
              // and that it is in fact a pressure dof
              Assert (stokes_dof_handler.locally_owned_dofs().is_element(first_pressure_dof),
                      ExcInternalError());
              Assert (local_dof_indices[first_pressure_dof] >= vector.block(0).size(),
                      ExcInternalError());

              // then adjust its value
              vector(local_dof_indices[first_pressure_dof]) += adjust;
            }
      }
  }



// This routine adjusts the second block of the right hand side of the
// system containing the compressibility, so that the system becomes
// compatible: 0=\int div u = \int g
// the helper vector h contains h_i=(q_i,1) with the pressure functions q_i
// and we adjust the right hand side g by h_i \int g / |\Omega|
  template <int dim>
  void Simulator<dim>::make_pressure_rhs_compatible(TrilinosWrappers::MPI::BlockVector &vector,
                                                    const TrilinosWrappers::MPI::BlockVector &helper)
  {
    if (parameters.use_locally_conservative_discretization)
      throw ExcNotImplemented();

    double mean = vector.block(1).mean_value();
    double correct = -mean*vector.block(1).size()/global_volume;

//  pcout << "    pressure correction: " << correct << std::endl;
    vector.block(1).add(correct, helper.block(1));
  }




  template <int dim>
  void Simulator<dim>::postprocess ()
  {
    computing_timer.enter_section ("Postprocessing");
    pcout << "   Postprocessing:" << std::endl;

    // run all the postprocessing routines and then write
    // the current state of the statistics table to a file
    std::list<std::pair<std::string,std::string> >
    output_list = postprocess_manager.execute (statistics);

    std::ofstream stat_file ("bin/statistics");
    statistics.set_scientific("Time (years)", true);
    statistics.set_scientific("Time step size (year)", true);
    statistics.write_text (stat_file,
                           TableHandler::table_with_separate_column_description);

    // determine the width of the first column of text so that
    // everything gets nicely aligned; then output everything
    {
      unsigned int width = 0;
      for (std::list<std::pair<std::string,std::string> >::const_iterator
           p = output_list.begin();
           p != output_list.end(); ++p)
        width = std::max<unsigned int> (width, p->first.size());

      for (std::list<std::pair<std::string,std::string> >::const_iterator
           p = output_list.begin();
           p != output_list.end(); ++p)
        pcout << "     "
              << std::left
              << std::setw(width)
              << p->first
              << " "
              << p->second
              << std::endl;
    }

    pcout << std::endl;
    computing_timer.exit_section ();
  }



// Contrary to step-32, we have found that just refining by the temperature
// works well in 2d, but only leads to refinement in the boundary
// layer at the core-mantle boundary in 3d. consequently, we estimate
// the error based both on the temperature and on the velocity; the
// vectors with the resulting error indicators are then both normalized
// to a maximal value of one, and we take the maximum of the two indicators
// to decide whether we want to refine or not. this ensures that we
// also refine into plumes where maybe the temperature gradients aren't
// as strong as in the boundary layer but where nevertheless the gradients
// in the velocity are large
  template <int dim>
  void Simulator<dim>::refine_mesh (const unsigned int max_grid_level)
  {
    computing_timer.enter_section ("Refine mesh structure, part 1");

    Vector<float> estimated_error_per_cell (triangulation.n_active_cells());

    // compute the errors for
    // temperature and stokes solution,
    // then scale them and find the
    // maximum between the two
    {
      Vector<float> estimated_error_per_cell_T (triangulation.n_active_cells());

      KellyErrorEstimator<dim>::estimate (temperature_dof_handler,
                                          QGauss<dim-1>(parameters.temperature_degree+1),
                                          typename FunctionMap<dim>::type(),
                                          temperature_solution,
                                          estimated_error_per_cell_T,
                                          std::vector<bool>(),
                                          0,
                                          0,
                                          triangulation.locally_owned_subdomain());
      estimated_error_per_cell_T /= Utilities::MPI::max (estimated_error_per_cell_T.linfty_norm(),
                                                         MPI_COMM_WORLD);

      Vector<float> estimated_error_per_cell_u (triangulation.n_active_cells());
      std::vector<bool> velocity_mask (dim+1, true);
      velocity_mask[dim] = false;
      KellyErrorEstimator<dim>::estimate (stokes_dof_handler,
                                          QGauss<dim-1>(parameters.stokes_velocity_degree+1),
                                          typename FunctionMap<dim>::type(),
                                          stokes_solution,
                                          estimated_error_per_cell_u,
                                          velocity_mask,
                                          0,
                                          0,
                                          triangulation.locally_owned_subdomain());
      estimated_error_per_cell_u /= Utilities::MPI::max (estimated_error_per_cell_u.linfty_norm(),
                                                         MPI_COMM_WORLD);

      for (unsigned int i=0; i<estimated_error_per_cell.size(); ++i)
        estimated_error_per_cell(i) = std::max (estimated_error_per_cell_T(i),
                                                estimated_error_per_cell_u(i));
    }

    parallel::distributed::GridRefinement::
    refine_and_coarsen_fixed_fraction (triangulation,
                                       estimated_error_per_cell,
                                       parameters.refinement_fraction,
                                       parameters.coarsening_fraction);

    // limit maximum refinement level
    if (triangulation.n_levels() > max_grid_level)
      for (typename Triangulation<dim>::active_cell_iterator
           cell = triangulation.begin_active(max_grid_level);
           cell != triangulation.end(); ++cell)
        cell->clear_refine_flag ();

    std::vector<const TrilinosWrappers::MPI::Vector *> x_temperature (2);
    x_temperature[0] = &temperature_solution;
    x_temperature[1] = &old_temperature_solution;
    std::vector<const TrilinosWrappers::MPI::BlockVector *> x_stokes (2);
    x_stokes[0] = &stokes_solution;
    x_stokes[1] = &old_stokes_solution;

    parallel::distributed::SolutionTransfer<dim,TrilinosWrappers::MPI::Vector>
    temperature_trans(temperature_dof_handler);
    parallel::distributed::SolutionTransfer<dim,TrilinosWrappers::MPI::BlockVector>
    stokes_trans(stokes_dof_handler);

    triangulation.prepare_coarsening_and_refinement();
    temperature_trans.prepare_for_coarsening_and_refinement(x_temperature);
    stokes_trans.prepare_for_coarsening_and_refinement(x_stokes);

    triangulation.execute_coarsening_and_refinement ();
    global_volume = GridTools::volume (triangulation, mapping);
    computing_timer.exit_section();

    setup_dofs ();

    computing_timer.enter_section ("Refine mesh structure, part 2");

    {
      TrilinosWrappers::MPI::Vector
      distributed_temp1 (temperature_rhs);
      TrilinosWrappers::MPI::Vector
      distributed_temp2 (temperature_rhs);

      std::vector<TrilinosWrappers::MPI::Vector *> tmp (2);
      tmp[0] = &(distributed_temp1);
      tmp[1] = &(distributed_temp2);
      temperature_trans.interpolate(tmp);

      temperature_solution     = distributed_temp1;
      old_temperature_solution = distributed_temp2;
    }

    {
      TrilinosWrappers::MPI::BlockVector
      distributed_stokes (stokes_rhs);
      TrilinosWrappers::MPI::BlockVector
      old_distributed_stokes (stokes_rhs);
      std::vector<TrilinosWrappers::MPI::BlockVector *> stokes_tmp (2);
      stokes_tmp[0] = &(distributed_stokes);
      stokes_tmp[1] = &(old_distributed_stokes);

      stokes_trans.interpolate (stokes_tmp);
      stokes_solution     = distributed_stokes;
      old_stokes_solution = old_distributed_stokes;
    }

    computing_timer.exit_section();
  }



  template <int dim>
  void Simulator<dim>::run ()
  {
    if (parameters.resume_computation == true)
      {
        resume_from_snapshot();
      }
    else
      {
        triangulation.refine_global (parameters.initial_global_refinement);
        global_volume = GridTools::volume (triangulation, mapping);

        setup_dofs();
      }

    unsigned int max_refinement_level = parameters.initial_global_refinement +
                                        parameters.initial_adaptive_refinement;

    unsigned int pre_refinement_step = 0;

  start_time_iteration:

    if (parameters.resume_computation == false)
      {
        set_initial_temperature_field ();
        compute_initial_pressure_field ();

        time                      = 0;
        timestep_number           = 0;
        time_step = old_time_step = 0;
      }

    do
      {
        pcout << "*** Timestep " << timestep_number
              << ":  t=" << time/year_in_seconds
              << " years"
              << std::endl;

        // set global statistics about this time step
        statistics.add_value("Time step number", timestep_number);
        statistics.add_value("Time (years)", time / year_in_seconds);

        assemble_stokes_system ();
        build_stokes_preconditioner ();

        solve ();

        pcout << std::endl;

        // see if we have to start over with a new refinement cycle
        // at the beginning of the simulation
        if ((timestep_number == 0) &&
            (pre_refinement_step < parameters.initial_adaptive_refinement))
          {
            refine_mesh (max_refinement_level);
            ++pre_refinement_step;
            goto start_time_iteration;
          }

        postprocess ();

        // see if this is a time step where additional refinement is requested
        // if so, then loop over as many times as this is necessary
        if ((parameters.additional_refinement_times.size() > 0)
            &&
            (parameters.additional_refinement_times.front () < time+time_step))
          {
            while ((parameters.additional_refinement_times.size() > 0)
                   &&
                   (parameters.additional_refinement_times.front () < time+time_step))
              {
                ++max_refinement_level;
                refine_mesh (max_refinement_level);

                parameters.additional_refinement_times
                .erase (parameters.additional_refinement_times.begin());
              }
          }
        else
          // see if this is a time step where regular refinement is necessary, but only
          // if the previous rule wasn't triggered
          if ((timestep_number > 0)
              &&
              (timestep_number % parameters.adaptive_refinement_interval == 0))
            refine_mesh (max_refinement_level);

        // prepare for the next time
        // step
        TrilinosWrappers::MPI::BlockVector old_old_stokes_solution;
        old_old_stokes_solution      = old_stokes_solution;
        old_stokes_solution          = stokes_solution;
        old_old_temperature_solution = old_temperature_solution;
        old_temperature_solution     = temperature_solution;
        if (old_time_step > 0)
          {
            stokes_solution.sadd (1.+time_step/old_time_step, -time_step/old_time_step,
                                  old_old_stokes_solution);
            temperature_solution.sadd (1.+time_step/old_time_step,
                                       -time_step/old_time_step,
                                       old_old_temperature_solution);
          }

        // every 100 time steps output
        // a summary of the current
        // timing information
        if ((timestep_number > 0) && (timestep_number % 100 == 0))
          computing_timer.print_summary ();

        time += time_step;
        ++timestep_number;

        if (timestep_number % 50 == 0)
          {
            create_snapshot();
            // matrices will be regenerated after a resume, so do that here too
            // to be consistent.
            rebuild_stokes_matrix =
              rebuild_stokes_preconditioner = true;
          }

        // if we are at the end of
        // time, stop now
        if (time > parameters.end_time * year_in_seconds)
          break;
      }
    while (true);
  }
}



// explicit instantiation of the functions we implement in this file
namespace aspect
{
  template
  class Simulator<deal_II_dimension>;
}