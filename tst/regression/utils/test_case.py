#!/usr/bin/env python3
#========================================================================================
# Athena++ astrophysical MHD code
# Copyright(C) 2014 James M. Stone <jmstone@princeton.edu> and other code contributors
# Licensed under the 3-clause BSD License, see LICENSE file for details
#========================================================================================
# (C) (or copyright) 2020. Triad National Security, LLC. All rights reserved.
#
# This program was produced under U.S. Government contract 89233218CNA000001 for Los
# Alamos National Laboratory (LANL), which is operated by Triad National Security, LLC
# for the U.S. Department of Energy/National Nuclear Security Administration. All rights
# in the program are reserved by Triad National Security, LLC, and the U.S. Department
# of Energy/National Nuclear Security Administration. The Government is granted for
# itself and others acting on its behalf a nonexclusive, paid-up, irrevocable worldwide
# license in this material to reproduce, prepare derivative works, distribute copies to
# the public, perform publicly and display publicly, and to permit others to do so.
#========================================================================================

import os
from shutil import rmtree

class Parameters():
    def __init__(self):
        driver_path = ""
        driver_input_path = ""
        test_path = ""
        output_path = ""
        mpi_cmd = ""
        mpi_opts = ""
class TestCase:


    def __init__(self,run_test_path,**kwargs):

        self.parameters = Parameters()
        self.__run_test_py_path = run_test_path 
        self.__regression_test_suite_path = os.path.join(self.__run_test_py_path,'test_suites')
        test_dir = kwargs.pop('test_dir')
        parthenon_driver = kwargs.pop('driver')
        parthenon_driver_input = kwargs.pop('driver_input')

        self.__initial_working_dir = os.getcwd()

        test_path = self.__checkAndGetRegressionTestFolder(test_dir[0])

        test_base_name = os.path.split(test_path)[1]
        self.test = os.path.basename(os.path.normpath(test_path))

        self.__checkRegressionTestScript(test_base_name)
        self.__checkDriverPath(parthenon_driver[0])
        self.__checkDriverInputPath(parthenon_driver_input[0])

        driver_path = os.path.abspath(parthenon_driver[0])
        driver_input_path = os.path.abspath(parthenon_driver_input[0])
        output_path = test_path + "/output"
        self.__test_module = 'test_suites.' + test_base_name + '.' + test_base_name

        test_module = 'test_suites.' + test_base_name + '.' + test_base_name
        output_msg = "Using:\n"
        output_msg += "driver at:       " + driver_path + "\n"
        output_msg += "driver input at: " + driver_input_path + "\n"
        output_msg += "test folder:     " + test_path + "\n"
        output_msg += "output sent to:  " + output_path + "\n"
        print(output_msg)

        self.parameters.driver_path = driver_path
        self.parameters.driver_input_path = driver_input_path
        self.parameters.output_path = output_path
        self.parameters.test_path = test_path
        self.parameters.mpi_cmd = kwargs.pop('mpirun')
        self.parameters.mpi_opts = kwargs.pop('mpirun_opts')

    def __checkAndGetRegressionTestFolder(self,test_dir):
        if not os.path.isdir(test_dir) :
            if not os.path.isdir( os.path.join('test_suites',test_dir)):
                error_msg = "Regression test folder is unknown: " + test_dir + "\n"
                error_msg +="looked in:\n" 
                error_msg += "  parthenon/tst/regression/test_suites/" + test_dir + "\n" 
                error_msg += "  " + test_dir + "\n"
                error_msg += "Each regression test must have a folder in "
                error_msg += "parthenon/regression/tst/test_suites.\n"
                error_msg += "Known tests folders are:"
                known_test_folders = os.listdir(self.__regression_test_suite_path)
                for folder in known_test_folders:
                    error_msg += "\n  " + folder 

                raise TestCaseError(error_msg)
            else:
                return os.path.abspath(os.path.join('test_suites',test_dir))
        else:
            return os.path.abspath(test_dir)

    def __checkRegressionTestScript(self,test_base_name):
        if not os.path.isfile(os.path.join('test_suites',test_base_name,test_base_name + ".py")) :
            error_msg = "Missing regression test file "
            error_msg += os.path.join('test_suites',test_base_name,test_base_name + ".py")
            error_msg += "\nEach test folder must have a python script with the same name as the "
            error_msg += "regression test folder."
            raise TestCaseError(error_msg)

    def __checkDriverPath(self,parthenon_driver):
        if not os.path.isfile(parthenon_driver):
            raise TestCaseError("Unable to locate driver "+parthenon_driver)

    def __checkDriverInputPath(self,parthenon_driver_input):
        if not os.path.isfile(parthenon_driver_input):
            raise TestCaseError("Unable to locate driver input file " + parthenon_driver_input)

    def CleanOutputFolder(self):
        if os.path.isdir(self.parameters.output_path):
                rmtree(self.parameters.output_path)

        os.mkdir(self.parameters.output_path)
        os.chdir(self.parameters.output_path)

    def RunAnalyze(self):

        module = __import__(self.__test_module, globals(), locals(),
                fromlist=['run', 'analyze'])

        test_pass = False

        try:
            module.run(self.parameters)
        except Exception:
            raise TestCaseError("Failed to run driver for test " + self.test) 

        try:
            test_pass = module.analyze(self.parameters)
        except: 
            raise TestCaseError("Error in analyzing test criteria for test " + self.test)

        return test_pass

# Exception for unexpected behavior by individual tests
class TestCaseError(RuntimeError):
    pass



