// Copyright 2008 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//
const char *help = "\
gradient_covariance_estimator\n\
\n\
This program loads a model and some data, then it: estimates the leading\n\
(largest) eigen values-vectors of the covariance of the gradients using\n\
the pca_estimator on the gradients.\n";

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cassert>

#include "Allocator.h"
#include "CmdLine.h"
#include "DiskXFile.h"
#include "MatDataSet.h"
#include "ClassFormatDataSet.h"
#include "OneHotClassFormat.h"
#include "ClassNLLCriterion.h"
#include "matrix.h"
#include "Parameters.h"
#include  "communicating_stacked_autoencoder.h"
#include "pca_estimator.h"
#include "helpers.h"
#include "analysis_utilities.h"

using namespace Torch;

// ************
// *** MAIN ***
// ************
int main(int argc, char **argv)
{

  // The command-line
  int flag_n_inputs;
  int flag_n_classes;
  char *flag_data_filename;
  char *flag_model_filename;

  int flag_n_eigen;
  int flag_minibatch_size;
  real flag_gamma;
  int flag_iterations;

  int flag_max_load;
  bool flag_binary_mode;

  CmdLine cmd;
  cmd.info(help);

  cmd.addICmdArg("-n_inputs", &flag_n_inputs, "number of inputs");
  cmd.addICmdArg("-n_classes", &flag_n_classes, "number of targets");
  cmd.addSCmdArg("-data_filename", &flag_data_filename, "Filename for the data.");
  cmd.addSCmdArg("-model_filename", &flag_model_filename, "the model filename");

  cmd.addICmdOption("-n_eigen", &flag_n_eigen, 10, "number of eigen values in the low rank estimate", true);
  cmd.addICmdOption("-minibatch_size", &flag_minibatch_size, 10, "number of observations before a reevaluation", true);
  cmd.addRCmdOption("-gamma", &flag_gamma, 0.999, "discount factor", true);
  cmd.addICmdOption("-iterations", &flag_iterations, 1, "number of iterations over the data", true);

  cmd.addICmdOption("-max_load", &flag_max_load, -1, "max number of examples to load for train", true);
  cmd.addBCmdOption("-binary_mode", &flag_binary_mode, false, "binary mode for files", true);

  cmd.read(argc, argv);

  // Allocator
  Allocator *allocator = new Allocator;

  // Data
  MatDataSet matdata(flag_data_filename, flag_n_inputs, 1, false,
                                flag_max_load, flag_binary_mode);
  ClassFormatDataSet data(&matdata,flag_n_classes);
  OneHotClassFormat class_format(&data);

  // Load the model
  CommunicatingStackedAutoencoder *csae = LoadCSAE(allocator, flag_model_filename);

  // Criterion
  ClassNLLCriterion criterion(&class_format);
  
  // The parameters of the model
  Parameters *der_params = csae->der_params;

  int n_param_groups = der_params->n_data;
  std::cout << n_param_groups << " groups of parameters." << std::endl;
  assert(n_param_groups == csae->n_hidden_layers+1);

  int n_params = GetNParams(csae);
  std::cout << n_params << " parameters!" << std::endl;

  // Estimator - Just one 
  PcaEstimator *estimator = new(allocator) PcaEstimator(n_params, flag_n_eigen, flag_minibatch_size, flag_gamma);

  // Iterate over the data
  csae->setDataSet(&data);
  criterion.setDataSet(&data);

  Vec sample(n_params);
  int tick = 1;

  for (int it=0; it<flag_iterations; it++)  {
    for (int i=0; i<data.n_examples; i++)  {

      data.setExample(i);

      // fbprop
      csae->forward(data.inputs);
      criterion.forward(csae->outputs);

      criterion.backward(csae->outputs, NULL);
      csae->backward(data.inputs, criterion.beta);
    
      // Observe the gradients - copy to sample
      int offset = 0;
      for(int j=0; j<der_params->n_data; j++) {
        memcpy(sample.ptr+offset, der_params->data[j], der_params->size[j] * sizeof(real));
        offset += der_params->size[j];
      }
      estimator->Observe(&sample);

      ClearDerivatives(csae);
 
      // Progress
      if ( (real)i/data.n_examples > tick/100.0)  {
        std::cout << ".";
        flush(std::cout);
        tick++;
      }
   
    }
  }



  // Save the results in the 'hessian' folder

  // - create the folder
  warning("Calling non portable mkdir!");
  std::string command = "mkdir hessian";
  system(command.c_str());

  // - Grab and save the eigen values-vectors
  // Grab and print the eigen values vectors
  Vec *eigenvals;
  Mat *eigenvecs;
  eigenvals = new(allocator) Vec(flag_n_eigen);
  eigenvecs = new(allocator) Mat(flag_n_eigen, n_params);

  estimator->GetLeadingEigen(eigenvals, eigenvecs);

  for (int j=0; j<eigenvals->n; j++)
    std::cout << eigenvals->ptr[j] << std::endl;

  // save eigenvals to a file
  std::ofstream fd_eigenval;
  std::stringstream ss_filename;
  ss_filename << "hessian/nb_eigenvals.txt";
  fd_eigenval.open(ss_filename.str().c_str());
  if (!fd_eigenval.is_open())
    error("Can't open hessian/nb_eigenvals.txt");

  for (int j=0; j<eigenvals->n; j++)
    fd_eigenval << eigenvals->ptr[j] << std::endl;

  fd_eigenval.close();

  // save eigenvecs to a file
  std::ofstream fd_eigenvec;
  ss_filename.str("");
  ss_filename.clear();
  ss_filename << "hessian/nb_eigenvecs.txt";
  fd_eigenvec.open(ss_filename.str().c_str());
  if (!fd_eigenvec.is_open())
    error("Can't open hessian/nb_eigenvecs.txt");

  for (int j=0; j<eigenvecs->m; j++)  {
    for (int k=0; k<eigenvecs->n; k++)
      fd_eigenvec << eigenvecs->ptr[j][k] << " ";
    fd_eigenvec << std::endl;
  }

    fd_eigenvec.close();
  
  delete allocator;
  return(0);
}


