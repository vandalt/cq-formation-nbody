#include <mpi.h>

#include "global.h"
#include <vector>
#include <boost/mpi.hpp>
using namespace boost::mpi;
using namespace std;

int mpiRank = 0;
int mpiSize = 1;
communicator world;
vector<int> counts1d, counts3d, displs1d, displs3d;

double drandom(double x,double y)
{
  double out = VRG(gen);
  out = x + (y - x)*out;
  return out;
}

void write_state(int timestep,const vector<double> & x)
{
  int i,zero = 0;
  std::stringstream sstream;
  sstream << "nbody_" << timestep << ".mol";
  std::string filename = sstream.str();
  std::ofstream s(filename.c_str());
  s << "nbody_" << timestep << std::endl;
  s << "  MOE2000" << std::endl;
  s << std::endl;
  s << std::setw(3) << NP << std::setw(3) << zero << " 0  0  0  0  0  0  0  0   1 V2000" << std::endl;
  for(i=0; i<NP; ++i) {
    s << std::setw(10) << std::setprecision(4) << std::setiosflags(std::ios::fixed) << x[3*i] << std::setw(10) << std::setprecision(4) << std::setiosflags(std::ios::fixed) << x[3*i+1] << std::setw(10) << std::setprecision(4) << std::setiosflags(std::ios::fixed) << x[3*i+2] << " " << std::setw(3) << std::setiosflags(std::ios::left) << "C" << std::resetiosflags(std::ios::left) << " 0  0  0  0  0  0  0  0  0  0  0  0" << std::endl;
  }
  s << "M  END" << std::endl;
  s << "$$$$" << std::endl;
  s.close();
}

void boundary_conditions(vector<double> & x)
{
  int i,j;
  double xv,dsize;

  if (!finite_domain) return;

  for(i=0; i<NP; ++i) {
    for(j=0; j<3; ++j) {
      xv = x[3*i+j];
      if (xv < L[2*j]) {
        dsize = L[2*j+1] - L[2*j];
        do {
          xv += dsize;
          if (xv > L[2*j]) break;
        } while(true);
        x[3*i+j] = xv;
      }
      else if (xv > L[2*j+1]) {
        dsize = L[2*j+1] - L[2*j];
        do {
          xv -= dsize;
          if (xv < L[2*j+1]) break;
        } while(true);
        x[3*i+j] = xv;
      }
    }
  }
}

void compute_acceleration(vector<double> & x,const vector<double> & mass,vector<double> & acc)
{
  const int iLow  = displs1d[mpiRank];
  const int iHigh = displs1d[mpiRank] + counts1d[mpiRank];
  int i,j,k;
  double delta,rij,pfactor,sum[3];

  broadcast(world, x, 0);

  for(i=iLow; i<iHigh; ++i) {
    for(j=0; j<3; ++j) {
      sum[j] = 0.0;
    }
    for(j=0; j<NP; ++j) {
      if (i == j) continue;
      delta = 0.0;
      for(k=0; k<3; ++k) {
        delta += (x[3*i+k] - x[3*j+k])*(x[3*i+k] - x[3*j+k]);
      }
      rij = std::sqrt(epsilon + delta);
      pfactor = mass[j]/(rij*rij*rij);
      for(k=0; k<3; ++k) {
        sum[k] += pfactor*(x[3*i+k] - x[3*j+k]);
      }
    }
    for(j=0; j<3; ++j) {
      acc[3*i+j] = -sum[j];
    }
  }

//  all_gather(world, &acc[displs3d[mpiRank]], counts3d[mpiRank], acc);
    gatherv(world, &acc[displs3d[mpiRank]], counts3d[mpiRank], &acc[0], counts3d, displs3d, 0);
}

double compute_energy(const vector<double> & x,const vector<double> & v,const vector<double> & mass)
{
  int i,j,k;
  double delta,T = 0.0,U = 0.0;

  // First the kinetic energy...
  for(i=0; i<NP; ++i) {
    delta = 0.0;
    for(j=0; j<3; ++j) {
      delta += v[3*i+j]*v[3*i+j];
    }
    T += mass[i]*delta;
  }
 
  // Now the potential energy
  for(i=0; i<NP; ++i) {
    for(j=1+i; j<NP; ++j) {
      delta = 0.0;
      for(k=0; k<3; ++k) {
        delta += (x[3*i+k] - x[3*j+k])*(x[3*i+k] - x[3*j+k]);
      }
      U += mass[i]*mass[j]/std::sqrt(epsilon + delta); 
    }
  }
  return (0.5*T - U);  
}

double compute_kinetic_energy(const vector<double> & x,const vector<double> & v,const vector<double> & mass)
{
  int i,j;
  double delta,T = 0.0;

  for(i=0; i<NP; ++i) {
    delta = 0.0;
    for(j=0; j<3; ++j) {
      delta += v[3*i+j]*v[3*i+j];
    }
    T += mass[i]*delta;
  }
 
  return 0.5*T;  
}

double compute_potential_energy(const vector<double> & x,const vector<double> & v,const vector<double> & mass)
{
  int i,j,k;
  double delta,U = 0.0;

  for(i=0; i<NP; ++i) {
    for(j=1+i; j<NP; ++j) {
      delta = 0.0;
      for(k=0; k<3; ++k) {
        delta += (x[3*i+k] - x[3*j+k])*(x[3*i+k] - x[3*j+k]);
      }
      U += mass[i]*mass[j]/std::sqrt(epsilon + delta); 
    }
  }
  return U;  
}

void compute_center_of_mass(const vector<double> & x, const vector<double> & mass, vector<double> & center)
{
  int i,j;
  double total_mass = 0.0;

  for(j=0; j<3; ++j) {
    center[j] = 0.0;
  }
  for(i=0; i<NP; ++i) {
    for(j=0; j<3; ++j) {
      center[j] += x[3*i+j]*mass[i];
    }
    total_mass += mass[i];
  }
  for(j=0; j<3; ++j) {
    center[j] /= total_mass;
  }
}

void center_particles(vector<double> & x, const vector<double> & mass)
{
  int i,j;
  vector<double> center(3);
  compute_center_of_mass(x,mass,center);
  for(i=0; i<NP; ++i) {
    for(j=0; j<3; ++j) {
      x[3*i+j] -= center[j];
    }
  }
}

void integrate()
{
  int i,j,l;
  vector<double> x(3*NP), xnew(3*NP), v(3*NP), vnew(3*NP);
  vector<double> mass(NP), acc(3*NP), temp(3*NP);
  double K,U,alpha;

  if (mpiRank == 0) {
    // Assign initial values...
    for(i=0; i<NP; ++i) {
      for(j=0; j<3; ++j) {
        // Initial position and speed
        x[3*i+j] = drandom(L[2*j],L[2*j+1]);
        v[3*i+j] = drandom(-0.2,0.2);
      }
    }
    // Assign random mass
    for(i=0; i<NP; ++i) {
      mass[i] = drandom(low_mass,high_mass);
    }

    // Add a rotation around the z axis
    for(i=0; i<NP; ++i) {
      v[3*i+1] += x[3*i+0]/10.0;
      v[3*i+0] -= x[3*i+1]/10.0;
    }

    if (center_masses) {
      // Set the center of mass and it's speed to 0
      center_particles(x, mass);
      center_particles(v, mass);
    }

    if (bounded_state) {
      // Make sure that the total energy of the system is negative so particle don't fly in the distance
      // Set the kinetic energy to half the potential energy
      U = compute_potential_energy(x,v,mass);
      K = compute_kinetic_energy(x,v,mass);
      alpha = std::sqrt(U/(2.0*K));

      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          v[3*i+j] *= alpha;
        }
      }
    }
  } // if (mpiRank == 0)

  broadcast(world, mass, 0);

  if (mpiRank == 0) {
    write_state(0,x);
    std::cout << "0.0  " << compute_energy(x,v,mass)/double(NP) << std::endl;
  }

#ifdef VERLET
  compute_acceleration(x,mass,acc);
  for(l=1; l<=NT; ++l) {
    if (mpiRank == 0) {
      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          xnew[3*i+j] = x[3*i+j] + dt*v[3*i+j] + 0.5*dt*dt*acc[3*i+j];
        }
      }
      boundary_conditions(xnew);
    }
    compute_acceleration(xnew,mass,temp);
    if (mpiRank == 0) {
      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          vnew[3*i+j] = v[3*i+j] + 0.5*dt*(acc[3*i+j] + temp[3*i+j]);
        }
      }

      // Print out the system's total energy per particle (should be fairly constant)
      if (l%write_freq == 0) {
        std::cout << dt*double(l) << "  " << compute_energy(x,v,mass)/double(NP) << std::endl;
      }
      if ((l % write_freq) == 0) write_state(l,xnew);

      // Now update the arrays
      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          x[3*i+j] = xnew[3*i+j];
          v[3*i+j] = vnew[3*i+j];
          acc[3*i+j] = temp[3*i+j];
        }
      }
    }
  }
#else
  // Fourth-order Runge-Kutta
  double k1[6*NP],k2[6*NP],k3[6*NP],k4[6*NP];
  
  for(l=1; l<=NT; ++l) {
    compute_acceleration(x,mass,acc);
    if (mpiRank == 0) {
      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          k1[3*i+j] = v[3*i+j];
          k1[3*NP+3*i+j] = acc[3*i+j];
          temp[3*i+j] = x[3*i+j] + 0.5*dt*k1[3*i+j];
        }
      }
    }
    compute_acceleration(temp,mass,acc);
    if (mpiRank == 0) {
      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          k2[3*i+j] = (1.0 + 0.5*dt)*v[3*i+j];
          k2[3*NP+3*i+j] = acc[3*i+j];
          temp[3*i+j] = x[3*i+j] + 0.5*dt*k2[3*i+j];
        }
      }
    }
    compute_acceleration(temp,mass,acc);
    if (mpiRank == 0) {
      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          k3[3*i+j] = (1.0 + 0.5*dt + 0.25*dt*dt)*v[3*i+j];
          k3[3*NP+3*i+j] = acc[3*i+j];
          temp[3*i+j] = x[3*i+j] + dt*k3[3*i+j];
        }
      }
    }
    compute_acceleration(temp,mass,acc);
    if (mpiRank == 0) {
      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          k4[3*i+j] = (1.0 + dt + 0.5*dt*dt + 0.25*dt*dt*dt)*v[3*i+j];
          k4[3*NP+3*i+j] = acc[3*i+j];
        }
      }
      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          xnew[3*i+j] = x[3*i+j] + dt*(k1[3*i+j] + 2.0*k2[3*i+j] + 2.0*k3[3*i+j]+k4[3*i+j])/6.0;
          vnew[3*i+j] = v[3*i+j] + dt*(k1[3*NP+3*i+j] + 2.0*k2[3*NP+3*i+j] + 2.0*k3[3*NP+3*i+j] + k4[3*NP+3*i+j])/6.0;
        }
      }
      boundary_conditions(xnew);

      // Print out the system's total energy per particle (should be fairly constant)
      if (l%write_freq == 0) {
        std::cout << dt*double(l) << "  " << compute_energy(xnew,vnew,mass)/double(NP) << std::endl;
      }
      if ((l % write_freq) == 0) write_state(l,xnew);

      // Now update the arrays
      for(i=0; i<NP; ++i) {
        for(j=0; j<3; ++j) {
          x[3*i+j] = xnew[3*i+j];
          v[3*i+j] = vnew[3*i+j];
        }
      }
    }
  }
#endif
  if (mpiRank = 0) {
    write_state(NT,x);
  }
}

void read_parameters(const char* filename)
{
  unsigned int i,bpoint;
  double tvalue;
  std::string line,name,value;

  std::ifstream s(filename);
  if (!s.is_open()) {
    // If the file doesn't exist, we need to exit...
    std::cout << "The file " << filename << " cannot be found!" << std::endl;
    environment::abort(1);
  }

  // Loop through all lines in the parameter file
  while(std::getline(s,line)) {
    // If it's an empty line, continue
    if (line.empty()) continue;
    // If the line begins with a #, ignore it
    if (line[0] == '#') continue;
    // If there's no equals sign in this line, continue
    if (line.find('=') == std::string::npos) continue;
    // Assumes that the equals sign can only occur once in 
    // the line
    bpoint = 0;
    name = "";
    for(i=0; i<line.size(); ++i) {
      if (line[i] == ' ') continue;
      if (line[i] == '=') {
        bpoint = i;
        break;
      }
      name += line[i];
    }
    value = "";
    for(i=1+bpoint; i<line.size(); ++i) {
      if (line[i] == ' ') continue;
      value += line[i];
    }
    // Now that we have the parameter name, see if it matches
    // any of the known parameters. If so, read in the value and
    // assign it
    if (name == "nparticle") {
      NP = stoi(value);
    }
    else if (name == "max_time") {
      tvalue = stod(value);
    }
    else if (name == "seed") {
      seed = stoi(value);
    }
    else if (name == "timestep") {
      dt = stod(value);
    }
    else if (name == "epsilon") {
      epsilon = stod(value);
    }
    else if (name == "min_mass") {
      low_mass = stod(value);
    }
    else if (name == "max_mass") {
      high_mass = stod(value);
    }
    else if (name == "write_frequency") {
      write_freq = stoi(value);
    }
    else if (name == "finite_domain") {
      finite_domain = (value == "yes") ? true : false;
    }
    else if (name == "center_of_mass") {
      center_masses = (value == "yes") ? true : false;
    }
    else if (name == "bound_state") {
      bounded_state = (value == "yes") ? true : false;
    }
    else if (name == "xmin") {
      L[0] = stod(value);
    }
    else if (name == "xmax") {
      L[1] = stod(value);
    }
    else if (name == "ymin") {
      L[2] = stod(value);
    }
    else if (name == "ymax") {
      L[3] = stod(value);
    }
    else if (name == "zmin") {
      L[4] = stod(value);
    }
    else if (name == "zmax") {
      L[5] = stod(value);
    }
  }
  s.close();
  // Sanity checks
  assert(tvalue > std::numeric_limits<double>::epsilon());
  assert(NP > 1);
  assert(dt > std::numeric_limits<double>::epsilon());
  assert(epsilon > std::numeric_limits<double>::epsilon() && epsilon < 0.1);
  assert(write_freq > 0);
  assert(low_mass > std::numeric_limits<double>::epsilon());
  assert(high_mass >= low_mass);
  assert(seed >= 0);
  for(int i=0; i<3; ++i) {
    assert(L[2*i+1] > L[2*i]);
  }
  if (seed == 0) seed = std::time(NULL);
  gen.seed(seed);
  NT = int(tvalue/dt);
}

int main(int argc,char** argv)
{
  environment env(argc,argv);
  mpiRank = world.rank();
  mpiSize = world.size();

  if (argc > 2) {
    if (mpiRank == 0) {
      std::cerr << "Usage: ./nbody parameters.txt" << std::endl;
    }
    environment::abort(1);
  }

  if (argc == 2) read_parameters(argv[1]);

  // Calculer les comptes et déplacements selon NP
  counts1d.resize(mpiSize);
  counts3d.resize(mpiSize);
  displs1d.resize(mpiSize);
  displs3d.resize(mpiSize);

  for (int r = 0; r < mpiSize; r++) {
    int curr = NP * (r + 0) / mpiSize;
    int next = NP * (r + 1) / mpiSize;

    // Intervalles de travail de chaque processus
    counts1d[r] = next - curr;
    displs1d[r] = curr;

    // Échange de 3 MPI_DOUBLE à la fois
    counts3d[r] = 3 * counts1d[r];
    displs3d[r] = 3 * displs1d[r];
  }

  integrate();

  return 0;
}

