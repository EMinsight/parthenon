//========================================================================================
// (C) (or copyright) 2020. Triad National Security, LLC. All rights reserved.
//
// This program was produced under U.S. Government contract 89233218CNA000001 for Los
// Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
// for the U.S. Department of Energy/National Nuclear Security Administration. All rights
// in the program are reserved by Triad National Security, LLC, and the U.S. Department
// of Energy/National Nuclear Security Administration. The Government is granted for
// itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
// license in this material to reproduce, prepare derivative works, distribute copies to
// the public, perform publicly and display publicly, and to permit others to do so.
//========================================================================================

#ifndef DRIVER_DRIVER_HPP_
#define DRIVER_DRIVER_HPP_

#include <limits>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "application_input.hpp"
#include "basic_types.hpp"
#include "globals.hpp"
#include "mesh/mesh.hpp"
#include "outputs/outputs.hpp"
#include "parameter_input.hpp"
#include "tasks/task_list.hpp"

namespace parthenon {

class Outputs;

enum class DriverStatus { complete, timeout, failed };

class Driver {
 public:
  Driver(ParameterInput *pin, ApplicationInput *app_in, Mesh *pm)
      : pinput(pin), app_input(app_in), pmesh(pm) {}
  virtual DriverStatus Execute() = 0;
  void InitializeOutputs() { pouts = std::make_unique<Outputs>(pmesh, pinput); }

  ParameterInput *pinput;
  ApplicationInput *app_input;
  Mesh *pmesh;
  std::unique_ptr<Outputs> pouts;

 protected:
  clock_t tstart_;
#ifdef OPENMP_PARALLEL
  double omp_start_time_;
#endif
  virtual void PreExecute();
  virtual void PostExecute();

 private:
};

class EvolutionDriver : public Driver {
 public:
  EvolutionDriver(ParameterInput *pin, ApplicationInput *app_in, Mesh *pm)
      : Driver(pin, app_in, pm) {
    Real start_time = pinput->GetOrAddPrecise("parthenon/time", "start_time", 0.0);
    Real tstop = pinput->GetOrAddPrecise("parthenon/time", "tlim",
                                         std::numeric_limits<Real>::infinity());
    Real dt =
        pinput->GetOrAddPrecise("parthenon/time", "dt", std::numeric_limits<Real>::max());
    int ncycle = pinput->GetOrAddInteger("parthenon/time", "ncycle", 0);
    int nmax = pinput->GetOrAddInteger("parthenon/time", "nlim", -1);
    int nout = pinput->GetOrAddInteger("parthenon/time", "ncycle_out", 1);
    tm = SimTime(start_time, tstop, nmax, ncycle, nout, dt);
    pouts = std::make_unique<Outputs>(pmesh, pinput, &tm);
  }
  DriverStatus Execute() override;
  void SetGlobalTimeStep();
  void OutputCycleDiagnostics();

  virtual TaskListStatus Step() = 0;
  SimTime tm;

 protected:
  virtual void PostExecute(DriverStatus status);

 private:
  void InitializeBlockTimeSteps();
};

namespace DriverUtils {

template <typename T, class... Args>
TaskListStatus ConstructAndExecuteBlockTasks(T *driver, Args... args) {
  int nmb = driver->pmesh->GetNumMeshBlocksThisRank(Globals::my_rank);
  TaskCollection tc;
  TaskRegion &tr = tc.AddRegion(nmb);

  int i = 0;
  for (auto &mb : driver->pmesh->block_list) {
    tr[i++] = driver->MakeTaskList(&mb, std::forward<Args>(args)...);
  }
  TaskListStatus status = tc.Execute();
  return status;
}

template <typename T, class... Args>
TaskListStatus ConstructAndExecuteTaskLists(T *driver, Args... args) {
  int nmb = driver->pmesh->GetNumMeshBlocksThisRank(Globals::my_rank);
  MeshBlock *pmb = driver->pmesh->pblock;
  std::vector<MeshBlock *> blocks(nmb);
  for (int i = 0; i < nmb; i++) {
    blocks[i] = pmb;
    pmb = pmb->next;
  }

  TaskCollection tc = driver->MakeTasks(blocks, std::forward<Args>(args)...);
  TaskListStatus status = tc.Execute();
  return status;
}

} // namespace DriverUtils

} // namespace parthenon

#endif // DRIVER_DRIVER_HPP_
