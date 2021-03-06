#include "analysis_utilities.h"

#include <string>
#include <sstream>
#include <iostream>
#include <fstream>
#include <cassert>

namespace Torch {

int GetNParams(GradientMachine *machine)
{
  int n_params = 0;
  for (int i=0; i<machine->params->n_data; i++)  {
    n_params += machine->params->size[i];
  }
  return n_params;
}

void ClearDerivatives(GradientMachine *machine)
{
  Parameters *der_params = machine->der_params;
  for(int i=0; i<der_params->n_data; i++) {
    for (int j=0; j<der_params->size[i]; j++)
      der_params->data[i][j] = 0.0;
  }
}

// 
void LoadDirections(char *directions_filename, int n_directions, Mat *directions)
{
  assert(directions_filename && directions);

  // Open the file
  std::ifstream fd_directions;
  fd_directions.open(directions_filename);
  if (!fd_directions.is_open())
    error("Can't open %s", directions_filename);

  // Load
  std::string line;
  std::stringstream tokens;
  int token_count;
  real token_value;

  for (int i=0; i<n_directions; i++)  {
    getline(fd_directions, line);
    tokens.str(line);
    tokens.clear();

    token_count = 0;
    while (tokens >> token_value) {
      if(token_count >= directions->n)
        error("LoadDirections(...) - too many tokens on the line!");
      directions->ptr[i][token_count] = token_value;
      token_count++;
    }

    if (token_count!=directions->n) {
      std::cout << token_count << " params loaded" << std::endl;
      error("LoadDirections(...) - error while loading a direction!");
    }
  }

  // Close file
  fd_directions.close();
}

void EvaluateGradient(GradientMachine *machine, Criterion *criterion, DataSet *data, Vec *gradient)
{
  // Preparation
  machine->setDataSet(data);
  criterion->setDataSet(data);

  for (int i=0; i<gradient->n; i++)
    gradient->ptr[i] = 0.0;

  ClearDerivatives(machine);

  // Go over the dataset, accumulating the gradients in the der_params 
  for (int i=0; i<data->n_examples; i++)  {
    data->setExample(i);
    // fbprop
    machine->forward(data->inputs);
    criterion->forward(machine->outputs);
    criterion->backward(machine->outputs, NULL);
    machine->backward(data->inputs, criterion->beta);
  }

  // Copy and normalize
  int offset = 0;
  Parameters *der_params = machine->der_params;
  for (int pg=0; pg<der_params->n_data; pg++) {   // pg = parameter group
    for (int i=0; i<der_params->size[pg]; i++)
      gradient->ptr[offset+i] = der_params->data[pg][i] / data->n_examples;
    offset += der_params->size[pg];
  }

  ClearDerivatives(machine);
}

real EvaluateGradientVarianceInDirection(GradientMachine *machine, Criterion *criterion, DataSet *data, Vec *direction, bool is_centered)
{
  machine->setDataSet(data);
  criterion->setDataSet(data);
  ClearDerivatives(machine);

  // Variables for holding the gradients
  Vec example_gradient(GetNParams(machine));
  Allocator allocator;
  real *gradients_in_direction = (real*) allocator.alloc(sizeof(real)*data->n_examples);
  real mean_gradient_in_direction = 0.0;

  // Go over the dataset,  
  for (int i=0; i<data->n_examples; i++)  {
    data->setExample(i);
    // fbprop
    machine->forward(data->inputs);
    criterion->forward(machine->outputs);
    criterion->backward(machine->outputs, NULL);
    machine->backward(data->inputs, criterion->beta);
    // Copy gradient to a vector
    int offset = 0;
    Parameters *der_params = machine->der_params;
    for (int pg=0; pg<der_params->n_data; pg++) {   // pg = parameter group
      for (int p=0; p<der_params->size[pg]; p++)
        example_gradient.ptr[offset+p] = der_params->data[pg][p];
      offset += der_params->size[pg];
      // Clear derivatives
      memset(der_params->data[pg], 0, sizeof(real)*der_params->size[pg]);
    }
    // Get gradient in direction
    gradients_in_direction[i] = direction->iP(&example_gradient);
    mean_gradient_in_direction += gradients_in_direction[i];
  }

  // Compute the mean gradient in the direction
  mean_gradient_in_direction /= data->n_examples;

  // Compute the variance
  real variance_in_direction = 0.0;
  real centered_gradient_in_direction = 0.0;
  for (int i=0; i<data->n_examples; i++)  {
    if (is_centered)  {
      centered_gradient_in_direction = gradients_in_direction[i] - mean_gradient_in_direction;
      variance_in_direction += centered_gradient_in_direction * centered_gradient_in_direction;
    } else  {
      variance_in_direction += gradients_in_direction[i] * gradients_in_direction[i];
    }
  }

  variance_in_direction /= (data->n_examples-1);

  return variance_in_direction;

}

void StepInParameterSpace(GradientMachine *machine, Vec *direction, real stepsize)
{
  int offset = 0;
  for (int i=0; i<machine->params->n_data; i++)  {
    real *ptr = machine->params->data[i];
    for (int j=0; j<machine->params->size[i]; j++)
      ptr[j] += stepsize * direction->ptr[offset+j];
    offset += machine->params->size[i];
  }
}


}
