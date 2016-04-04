// Copyright 2016 Chun Shen
#include <stdlib.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <string>

#include "./parameter.h"
#include "./EM_fields.h"

using namespace std;

EM_fields::EM_fields(ParameterReader* paraRdr_in) {
    initialization_status = 0;
    paraRdr = paraRdr_in;

    mode = paraRdr->getVal("mode");
    turn_on_bulk = paraRdr->getVal("turn_on_bulk");
    int atomic_number = paraRdr->getVal("atomic_number");
    int number_of_proton = paraRdr->getVal("number_of_proton");
    charge_fraction = (static_cast<double>(number_of_proton)
                       /static_cast<double>(atomic_number));

    double ecm = paraRdr->getVal("ecm");
    double gamma = ecm/2./0.938;  // proton mass: 0.938 GeV
    double beta = sqrt(1. - 1./(gamma*gamma));
    double beam_rapidity = atanh(beta);
    spectator_rap = beam_rapidity;
    // cout << "spectator rapidity = " << spectator_rap << endl;

    nucleon_density_grid_size = paraRdr->getVal("nucleon_density_grid_size");
    nucleon_density_grid_dx = paraRdr->getVal("nucleon_density_grid_dx");
    if (nucleon_density_grid_size <= 0) {
        cout << "EM_fields:: Error: Grid size for nucleon density profiles "
             << "needs to be larger than 0!" << endl;
        cout << "Current grid_size = " << nucleon_density_grid_size << endl;
        exit(1);
    }
    nucleon_density_grid_x_array = new double[nucleon_density_grid_size];
    nucleon_density_grid_y_array = new double[nucleon_density_grid_size];
    spectator_density_1 = new double* [nucleon_density_grid_size];
    spectator_density_2 = new double* [nucleon_density_grid_size];
    participant_density_1 = new double* [nucleon_density_grid_size];
    participant_density_2 = new double* [nucleon_density_grid_size];
    for (int i = 0; i < nucleon_density_grid_size; i++) {
        nucleon_density_grid_x_array[i] = (
            -(nucleon_density_grid_size-1)/2. + i*nucleon_density_grid_dx);
        nucleon_density_grid_y_array[i] = (
            -(nucleon_density_grid_size-1)/2. + i*nucleon_density_grid_dx);
        spectator_density_1[i] = new double[nucleon_density_grid_size];
        spectator_density_2[i] = new double[nucleon_density_grid_size];
        participant_density_1[i] = new double[nucleon_density_grid_size];
        participant_density_2[i] = new double[nucleon_density_grid_size];
        for (int j = 0; j < nucleon_density_grid_size; j++) {
            spectator_density_1[i][j] = 0.0;
            spectator_density_2[i][j] = 0.0;
            participant_density_1[i][j] = 0.0;
            participant_density_2[i][j] = 0.0;
        }
    }

    n_eta = paraRdr->getVal("n_eta");
    if (n_eta < 2) {
        cout << "EM_field: Error: n_eta needs to be at least 2" << endl;
        cout << "current n_eta = " << n_eta << endl;
        exit(1);
    }
    eta_grid = new double[n_eta];
    sinh_eta_array = new double[n_eta];
    cosh_eta_array = new double[n_eta];
    double deta = 2.*beam_rapidity/(n_eta - 1.);
    for (int i = 0; i < n_eta; i++) {
        eta_grid[i] = - beam_rapidity + i*deta;
        sinh_eta_array[i] = sinh(eta_grid[i]);
        cosh_eta_array[i] = cosh(eta_grid[i]);
    }

    read_in_densities("./results");

    if (mode == 0) {
        set_transverse_grid_points(0.2, 0.0);
    } else if (mode == 1) {
        read_in_freezeout_surface_points_VISH2p1("./results/surface.dat",
                                                 "./results/decdat2.dat");
    } else if (mode == 2) {
        set_tau_grid_points(0.0, 0.0, 0.0);
    } else if (mode == 3) {
        read_in_freezeout_surface_points_VISH2p1_boost_invariant(
                                                    "./results/surface.dat");
    } else {
        cout << "EM_fields:: Error: unrecognize mode! "
             << "mode = " << mode << endl;
        exit(1);
    }

    initialization_status = 1;
}

EM_fields::~EM_fields() {
    if (initialization_status == 1) {
        for (int i = 0; i < nucleon_density_grid_size; i++) {
            delete[] spectator_density_1[i];
            delete[] spectator_density_2[i];
            delete[] participant_density_1[i];
            delete[] participant_density_2[i];
        }
        delete[] spectator_density_1;
        delete[] spectator_density_2;
        delete[] participant_density_1;
        delete[] participant_density_2;
        delete[] nucleon_density_grid_x_array;
        delete[] nucleon_density_grid_y_array;

        delete[] eta_grid;
        delete[] sinh_eta_array;
        delete[] cosh_eta_array;

        cell_list.clear();
    }
    return;
}

void EM_fields::read_in_densities(string path) {
    // spectators
    ostringstream spectator_1_filename;
    spectator_1_filename << path << "/spectator_density_A_disk.dat";
    // spectator_1_filename << path
    //                      << "/spectator_density_A_fromSd_order_2.dat";
    ostringstream spectator_2_filename;
    spectator_2_filename << path << "/spectator_density_B_disk.dat";
    // spectator_2_filename << path
    //                      << "/spectator_density_B_fromSd_order_2.dat";
    read_in_spectators_density(spectator_1_filename.str(),
                               spectator_2_filename.str());
    // participants
    //ostringstream participant_1_filename;
    //participant_1_filename << path 
    //                       << "/nuclear_thickness_TA_fromSd_order_2.dat";
    //ostringstream participant_2_filename;
    //participant_2_filename << path 
    //                       << "/nuclear_thickness_TB_fromSd_order_2.dat";
    //read_in_participant_density(participant_1_filename.str(), 
    //                            participant_2_filename.str());
}

void EM_fields::read_in_spectators_density(string filename_1,
                                           string filename_2) {
    cout << "read in spectator density ...";
    ifstream spec1(filename_1.c_str());
    ifstream spec2(filename_2.c_str());

    for (int i = 0; i < nucleon_density_grid_size; i++) {
        for (int j = 0; j < nucleon_density_grid_size; j++) {
            spec1 >> spectator_density_1[i][j];
            spec2 >> spectator_density_2[i][j];
        }
    }

    spec1.close();
    spec2.close();
    cout << " done!" << endl;
}

void EM_fields::read_in_participant_density(string filename_1,
                                            string filename_2) {
    cout << "read in participant density ...";
    ifstream part1(filename_1.c_str());
    ifstream part2(filename_2.c_str());

    for (int i = 0; i < nucleon_density_grid_size; i++) {
        for (int j = 0; j < nucleon_density_grid_size; j++) {
            part1 >> participant_density_1[i][j];
            part2 >> participant_density_2[i][j];
        }
    }

    part1.close();
    part2.close();
    cout << " done!" << endl;
}

void EM_fields::set_tau_grid_points(double x_local, double y_local,
                                    double eta_local) {
    double EM_fields_grid_size = 15.0;
    double EM_fields_grid_dtau = 0.01;

    int number_of_points =
                static_cast<int>(EM_fields_grid_size/EM_fields_grid_dtau) + 1;
    for (int i = 0; i < number_of_points; i++) {
        double tau_local = 0.0 + i*EM_fields_grid_dtau;
        fluidCell cell_local;
        cell_local.tau = tau_local;
        cell_local.x = x_local;
        cell_local.y = y_local;
        cell_local.eta = eta_local;
        cell_list.push_back(cell_local);
    }
    EM_fields_array_length = cell_list.size();
    cout << "number of freeze-out cells: " << EM_fields_array_length << endl;
}

void EM_fields::set_transverse_grid_points(double tau_local, double eta_local) {
    double EM_fields_grid_size = 20.0;
    double EM_fields_grid_dx = 0.1;

    int number_of_points =
                static_cast<int>(EM_fields_grid_size/EM_fields_grid_dx) + 1;
    for (int i = 0; i < number_of_points; i++) {
        double x_local = - EM_fields_grid_size/2. + i*EM_fields_grid_dx;
        for (int j = 0; j < number_of_points; j++) {
            double y_local = - EM_fields_grid_size/2. + j*EM_fields_grid_dx;
            fluidCell cell_local;
            cell_local.tau = tau_local;
            cell_local.x = x_local;
            cell_local.y = y_local;
            cell_local.eta = eta_local;
            cell_list.push_back(cell_local);
        }
    }
    EM_fields_array_length = cell_list.size();
    cout << "number of freeze-out cells: " << EM_fields_array_length << endl;
}

void EM_fields::read_in_freezeout_surface_points_VISH2p1(string filename1,
                                                         string filename2) {
    // this function reads in the freeze out surface points from a text file
    ifstream FOsurf(filename1.c_str());
    ifstream decdat(filename2.c_str());
    cout << "read in freeze-out surface points from VISH2+1 outputs ...";
    // read in freeze-out surface positions
    double dummy;
    string input;
    double tau_local, x_local, y_local;
    double vx_local, vy_local;
    double T_local;
    FOsurf >> dummy;
    while (!FOsurf.eof()) {
        FOsurf >> tau_local >> x_local >> y_local >> dummy >> dummy >> dummy;
        getline(decdat, input, '\n');
        stringstream ss(input);
        ss >> dummy >> dummy >> dummy >> dummy;     // skip tau and da_i
        ss >> vx_local >> vy_local;                 // read in vx and vy
        ss >> dummy >> dummy >> T_local;            // read in temperature
        // the rest is discharded
        double u_tau_local = 1./sqrt(1. - vx_local*vx_local
                                     - vy_local*vy_local);
        double u_x_local = u_tau_local*vx_local;
        double u_y_local = u_tau_local*vy_local;
        for (int i = 0; i < n_eta; i++) {
            fluidCell cell_local;
            cell_local.mu_m = M_PI/2.*sqrt(6*M_PI)*T_local*T_local;  // GeV^2
            cell_local.eta = eta_grid[i];
            cell_local.tau = tau_local;
            cell_local.x = x_local;
            cell_local.y = y_local;

            // compute fluid velocity in t-xyz coordinate
            double u_t_local = u_tau_local*cosh_eta_array[i];
            double u_z_local = u_tau_local*sinh_eta_array[i];
            cell_local.beta.x = u_x_local/u_t_local;
            cell_local.beta.y = u_y_local/u_t_local;
            cell_local.beta.z = u_z_local/u_t_local;

            // push back the fluid cell into the cell list
            cell_list.push_back(cell_local);
        }
        FOsurf >> dummy;
    }
    FOsurf.close();
    decdat.close();
    cout << " done!" << endl;
    EM_fields_array_length = cell_list.size();
    cout << "number of freeze-out cells: " << EM_fields_array_length << endl;
}

void EM_fields::read_in_freezeout_surface_points_VISH2p1_boost_invariant(
                                                            string filename) {
    // this function reads in the freeze out surface points from a text file
    ifstream FOsurf(filename.c_str());
    cout << "read in freeze-out surface points from VISH2+1 boost invariant "
         << "outputs ...";
    // read in freeze-out surface positions
    double dummy;
    string input;
    double tau_local, x_local, y_local;
    double u_tau_local, u_x_local, u_y_local;
    double T_local;
    while (!FOsurf.eof()) {
        getline(FOsurf, input, '\n');
        stringstream ss(input);
        FOsurf >> tau_local >> x_local >> y_local >> dummy;  // eta_s = 0.0
        ss >> dummy >> dummy >> dummy >> dummy;   // skip surface vector da_mu
        // read in flow velocity
        ss >> u_tau_local >> u_x_local >> u_y_local >> dummy;  // u_eta = 0.0
        ss >> dummy >> dummy >> T_local;
        // the rest information is discarded
        for (int i = 0; i < n_eta; i++) {
            fluidCell cell_local;
            cell_local.mu_m = M_PI/2.*sqrt(6*M_PI)*T_local*T_local;  // GeV^2
            cell_local.eta = eta_grid[i];
            cell_local.tau = tau_local;
            cell_local.x = x_local;
            cell_local.y = y_local;

            // compute fluid velocity in t-xyz coordinate
            double u_t_local = u_tau_local*cosh_eta_array[i];
            double u_z_local = u_tau_local*sinh_eta_array[i];
            cell_local.beta.x = u_x_local/u_t_local;
            cell_local.beta.y = u_y_local/u_t_local;
            cell_local.beta.z = u_z_local/u_t_local;

            // push back the fluid cell into the cell list
            cell_list.push_back(cell_local);
        }
    }
    FOsurf.close();
    cout << " done!" << endl;
    EM_fields_array_length = cell_list.size();
    cout << "number of freeze-out cells: " << EM_fields_array_length << endl;
}

void EM_fields::calculate_EM_fields() {
    // this function calculates E and B fields
    double cosh_spectator_rap = cosh(spectator_rap);
    double sinh_spectator_rap = sinh(spectator_rap);

    double dx_sq = nucleon_density_grid_dx*nucleon_density_grid_dx;
    for (int i_array = 0; i_array < EM_fields_array_length; i_array++) {
        double field_x = cell_list[i_array].x;
        double field_y = cell_list[i_array].y;
        double field_tau = cell_list[i_array].tau;
        double field_eta = cell_list[i_array].eta;
        double temp_sum_Ex_spectator = 0.0e0;
        double temp_sum_Ey_spectator = 0.0e0;
        double temp_sum_Ez_spectator = 0.0e0;
        double temp_sum_Bx_spectator = 0.0e0;
        double temp_sum_By_spectator = 0.0e0;

        double z_local_spectator_1 = field_tau*sinh(field_eta - spectator_rap);
        double z_local_spectator_2 = field_tau*sinh(field_eta + spectator_rap);
        double z_local_spectator_1_sq = 
                                    z_local_spectator_1*z_local_spectator_1;
        double z_local_spectator_2_sq =
                                    z_local_spectator_2*z_local_spectator_2;

        for (int i = 0; i < nucleon_density_grid_size; i++) {
            double grid_x = nucleon_density_grid_x_array[i];
            for (int j = 0; j < nucleon_density_grid_size; j++) {
                double grid_y = nucleon_density_grid_y_array[j];
                double x_local = field_x - grid_x;
                double y_local = field_y - grid_y;
                double r_perp_local_sq = x_local*x_local + y_local*y_local;
                double r_spectator_1 = sqrt(
                                r_perp_local_sq + z_local_spectator_1_sq);
                double r_cubic_spectator_1 = (r_spectator_1*r_spectator_1
                                              *r_spectator_1);
                double r_spectator_2 = sqrt(
                                r_perp_local_sq + z_local_spectator_2_sq);
                double r_cubic_spectator_2 = (r_spectator_2*r_spectator_2
                                              *r_spectator_2);
                double spectator_integrand_1 = (
                                spectator_density_1[i][j]/r_cubic_spectator_1);
                double spectator_integrand_2 = (
                                spectator_density_2[i][j]/r_cubic_spectator_2);

                double Ex_spectator_integrand = x_local*(
                                spectator_integrand_1 + spectator_integrand_2);
                double Ey_spectator_integrand = y_local*(
                                spectator_integrand_1 + spectator_integrand_2);
                double Ez_spectator_integrand = (
                                  z_local_spectator_1*spectator_integrand_1
                                + z_local_spectator_2*spectator_integrand_2);
                double Bx_spectator_integrand = y_local*(
                                spectator_integrand_1 - spectator_integrand_2);
                double By_spectator_integrand = x_local*(
                                spectator_integrand_1 - spectator_integrand_2);
                temp_sum_Ex_spectator += Ex_spectator_integrand;
                temp_sum_Ey_spectator += Ey_spectator_integrand;
                temp_sum_Ez_spectator += Ez_spectator_integrand;
                temp_sum_Bx_spectator += Bx_spectator_integrand;
                temp_sum_By_spectator += By_spectator_integrand;
            }
        }
        cell_list[i_array].E_lab.x = (
            unit_convert*charge_fraction*alpha_EM
            *cosh_spectator_rap*temp_sum_Ex_spectator*dx_sq);
        cell_list[i_array].E_lab.y = (
            unit_convert*charge_fraction*alpha_EM
            *cosh_spectator_rap*temp_sum_Ey_spectator*dx_sq);
        cell_list[i_array].E_lab.z = (
            unit_convert*charge_fraction*alpha_EM*temp_sum_Ez_spectator*dx_sq);
        cell_list[i_array].B_lab.x = (
            unit_convert*charge_fraction*alpha_EM
            *((-sinh_spectator_rap)*temp_sum_Bx_spectator)*dx_sq);
        cell_list[i_array].B_lab.y = (
            unit_convert*charge_fraction*alpha_EM
            *(sinh_spectator_rap*temp_sum_By_spectator)*dx_sq);
        cell_list[i_array].B_lab.z = 0.0;

        if (i_array % static_cast<int>(EM_fields_array_length/10) == 0) {
            cout << "computing EM fields: " << setprecision(3)
                 << (static_cast<double>(i_array)
                     /static_cast<double>(EM_fields_array_length)*100)
                 << "\% done." << endl;
        }
    }
    return;
}

void EM_fields::output_EM_fields(string filename) {
    // this function outputs the computed E and B fields to a text file
    ofstream output_file(filename.c_str());
    // write a header first
    output_file << "# tau[fm]  x[fm]  y[fm]  eta  "
                << "eE_x[GeV^2]  eE_y[GeV^2]  eE_z[GeV^2]  "
                << "eB_x[GeV^2]  eB_y[GeV^2]  eB_z[GeV^2]" << endl;
    for (int i = 0; i < EM_fields_array_length; i++) {
        output_file << scientific << setprecision(8) << setw(15)
                    << cell_list[i].tau << "   " << cell_list[i].x << "   "
                    << cell_list[i].y << "   " << cell_list[i].eta << "   "
                    << cell_list[i].E_lab.x << "   "
                    << cell_list[i].E_lab.y << "   "
                    << cell_list[i].E_lab.z << "   "
                    << cell_list[i].B_lab.x << "   "
                    << cell_list[i].B_lab.y << "   "
                    << cell_list[i].B_lab.z << endl;
    }
    output_file.close();
    return;
}

void EM_fields::output_surface_file_with_drifting_velocity(string filename) {
    // this function outputs hypersurface file with drifting velocity
    // the format of the hypersurface file is compatible with MUSIC
    ofstream output_file(filename.c_str());
    if (mode == 1) {    // read in mode is from VISH2+1
        ifstream decdat("./results/decdat2.dat");
        string input;
        double dummy;
        double da0, da1, da2;
        double Edec, Tdec, muB, Pdec;
        double pi00, pi01, pi02, pi11, pi12, pi22, pi33;
        double bulkPi;
        for (int i = 0; i < EM_fields_array_length; i++) {
            getline(decdat, input, '\n');
            stringstream ss(input);
            ss >> dummy >> da0 >> da1 >> da2;  // read in da_i
            double da3 = 0.0;
            ss >> dummy >> dummy;              // pipe vx and vy to dummy
            ss >> Edec >> dummy >> Tdec >> muB >> dummy >> Pdec;
            double e_plus_P_over_T = (Edec + Pdec)/Tdec;
            ss >> pi33 >> pi00 >> pi01 >> pi02 >> pi11 >> pi12 >> pi22;
            double pi03 = 0.0;
            double pi13 = 0.0;
            double pi23 = 0.0;
            ss >> bulkPi;

            double u_t = 1./sqrt(1. - cell_list[i].beta.x*cell_list[i].beta.x
                                 - cell_list[i].beta.y*cell_list[i].beta.y
                                 - cell_list[i].beta.z*cell_list[i].beta.z);
            double u_x = cell_list[i].beta.x*u_t;
            double u_y = cell_list[i].beta.y*u_t;
            double u_z = cell_list[i].beta.z*u_t;
            double u_tau = (u_t*cosh(cell_list[i].eta)
                            - u_z*sinh(cell_list[i].eta));
            double u_eta = 0.0;            // for boost-invariant medium
            output_file << scientific << setprecision(8) << setw(15)
                        << cell_list[i].tau << "  " << cell_list[i].x << "  "
                        << cell_list[i].y << "  " << cell_list[i].eta << "  "
                        << da0 << "  " << da1 << "  " << da2 << "  "
                        << da3 << "  "
                        << u_tau << "  " << u_x << "  " << u_y << "  "
                        << u_eta << "  "
                        << Edec << "  " << Tdec << "  " << muB << "  "
                        << e_plus_P_over_T << "  "
                        << pi00 << "  " << pi01 << "  " << pi02 << "  "
                        << pi03 << "  " << pi11 << "  " << pi12 << "  "
                        << pi13 << "  " << pi22 << "  " << pi23 << "  "
                        << pi33 << "  ";
            if (turn_on_bulk == 1)
                output_file << scientific << setprecision(8) << setw(15)
                            << bulkPi << "  ";
            output_file << scientific << setprecision(8) << setw(15)
                        << cell_list[i].drift_u.tau << "  "
                        << cell_list[i].drift_u.x << "  "
                        << cell_list[i].drift_u.y << "  "
                        << cell_list[i].drift_u.eta;
            output_file << endl;
        }
        decdat.close();
    } else if (mode == 3) {
        ifstream decdat("./results/surface.dat");
        string input;
        double dummy;
        double da0, da1, da2, da3;
        double u_tau, u_x, u_y, u_eta;
        double Edec, Tdec, muB, Pdec;
        double pi00, pi01, pi02, pi03, pi11, pi12, pi13, pi22, pi23, pi33;
        double bulkPi;
        for (int i = 0; i < EM_fields_array_length; i++) {
            getline(decdat, input, '\n');
            stringstream ss(input);
            // pipe cell position to dummy
            ss >> dummy >> dummy >> dummy >> dummy;
            ss >> da0 >> da1 >> da2 >> da3;          // read in da_mu
            ss >> u_tau >> u_x >> u_y >> u_eta;      // read in u^\mu
            ss >> Edec >> dummy >> Tdec >> muB >> dummy >> Pdec;
            double e_plus_P_over_T = (Edec + Pdec)/Tdec;
            ss >> pi00 >> pi01 >> pi02 >> pi03 >> pi11 >> pi12 >> pi13
               >> pi22 >> pi23 >> pi33;
            ss >> bulkPi;

            output_file << scientific << setprecision(8) << setw(15)
                        << cell_list[i].tau << "  " << cell_list[i].x << "  "
                        << cell_list[i].y << "  " << cell_list[i].eta << "  "
                        << da0 << "  " << da1 << "  " << da2 << "  "
                        << da3 << "  "
                        << u_tau << "  " << u_x << "  " << u_y << "  "
                        << u_eta << "  "
                        << Edec << "  " << Tdec << "  " << muB << "  "
                        << e_plus_P_over_T << "  "
                        << pi00 << "  " << pi01 << "  " << pi02 << "  "
                        << pi03 << "  " << pi11 << "  " << pi12 << "  "
                        << pi13 << "  " << pi22 << "  " << pi23 << "  "
                        << pi33 << "  ";
            if (turn_on_bulk == 1)
                output_file << scientific << setprecision(8) << setw(15)
                            << bulkPi << "  ";
            output_file << scientific << setprecision(8) << setw(15)
                        << cell_list[i].drift_u.tau << "  "
                        << cell_list[i].drift_u.x << "  "
                        << cell_list[i].drift_u.y << "  "
                        << cell_list[i].drift_u.eta;
            output_file << endl;
        }
        decdat.close();
    }
    output_file.close();
}

void EM_fields::calculate_charge_drifting_velocity() {
    // this function calculates the drifting velocity of the fluid cell
    // included by the local EM fields

    cout << "calculating the charge drifiting velocity ... " << endl;

    // initialization
    double *E_lab = new double[3];
    double *B_lab = new double[3];
    double *beta = new double[3];
    double *E_lrf = new double[3];
    double *B_lrf = new double[3];
    double *drift_u = new double[4];

    // loop over evey fluid cell
    for (int i = 0; i < EM_fields_array_length; i++) {
        // we first boost the EM fields to local rest frame of the fluid cell
        E_lab[0] = cell_list[i].E_lab.x;
        E_lab[1] = cell_list[i].E_lab.y;
        E_lab[2] = cell_list[i].E_lab.z;
        B_lab[0] = cell_list[i].B_lab.x;
        B_lab[1] = cell_list[i].B_lab.y;
        B_lab[2] = cell_list[i].B_lab.z;
        beta[0] = cell_list[i].beta.x;
        beta[1] = cell_list[i].beta.y;
        beta[2] = cell_list[i].beta.z;
        Lorentz_boost_EM_fields(E_lab, B_lab, beta, E_lrf, B_lrf);

        // we calculate the drifting velocity in the local rest frame
        double mu_m = cell_list[i].mu_m;
        double q = 1.0;
        double qEx = q*E_lrf[0];
        double qEy = q*E_lrf[1];
        double qEz = q*E_lrf[2];
        double qBx = q*B_lrf[0];
        double qBy = q*B_lrf[1];
        double qBz = q*B_lrf[2];
        double denorm = (
                1./(mu_m*(qBx*qBx + qBy*qBy + qBz*qBz) + mu_m*mu_m*mu_m));
        double delta_v_x = (qEz*(qBx*qBz - qBy*mu_m) + qEy*(qBx*qBy + qBz*mu_m)
                            + qEx*(qBx*qBx + mu_m*mu_m))*denorm;
        double delta_v_y = (qEz*(qBy*qBz + qBx*mu_m) + qEx*(qBx*qBy - qBz*mu_m)
                            + qEy*(qBy*qBy + mu_m*mu_m))*denorm;
        double delta_v_z = (qEy*(qBy*qBz - qBx*mu_m) + qEx*(qBx*qBz + qBy*mu_m)
                            + qEz*(qBz*qBz + mu_m*mu_m))*denorm;
        double gamma = 1./sqrt(1. - delta_v_x*delta_v_x
                               - delta_v_y*delta_v_y - delta_v_z*delta_v_z);
        drift_u[0] = gamma;
        drift_u[1] = gamma*delta_v_x;
        drift_u[2] = gamma*delta_v_y;
        drift_u[3] = gamma*delta_v_z;
        // finally we boost the delta v back to longitudinal comving frame
        for (int i = 0; i < 3; i++) {  // prepare the velocity
            beta[i] = - beta[i];
        }
        lorentz_transform_vector_in_place(drift_u, beta);
        // transform to tau-eta coorrdinate with tilde{u}^eta = tau*u^eta
        double eta_s = cell_list[i].eta;
        double sinh_eta_s = sinh(eta_s);
        double cosh_eta_s = cosh(eta_s);
        double drift_u_tau = drift_u[0]*cosh_eta_s - drift_u[3]*sinh_eta_s;
        double drift_u_eta = - drift_u[0]*sinh_eta_s + drift_u[3]*cosh_eta_s;
        cell_list[i].drift_u.tau = drift_u_tau;
        cell_list[i].drift_u.x = drift_u[1];
        cell_list[i].drift_u.y = drift_u[2];
        cell_list[i].drift_u.eta = drift_u_eta;
    }

    // clean up
    delete [] drift_u;
    delete [] E_lab;
    delete [] B_lab;
    delete [] E_lrf;
    delete [] B_lrf;
    delete [] beta;
}

void EM_fields::lorentz_transform_vector_in_place(double *u_mu, double *v) {
// boost u^mu with velocity v and store the boost vector back in u^mu
// v is a 3 vector and u_mu is a 4 vector
    double v2 = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
    double vp = v[0]*u_mu[1] + v[1]*u_mu[2] + v[2]*u_mu[3];

    double gamma = 1./sqrt(1. - v2);
    double gamma_m_1 = gamma - 1.;

    double ene = u_mu[0];

    u_mu[0] = gamma*(ene - vp);
    for (int i = 1; i < 4; i++) {
        u_mu[i] = u_mu[i] + (gamma_m_1*vp/v2 - gamma*ene)*v[i-1];
    }
}

void EM_fields::Lorentz_boost_EM_fields(double *E_lab, double *B_lab,
        double *beta, double *E_prime, double *B_prime) {
    // this function perform Lorentz boost for E_lab and B_lab fields
    // to a frame with velocity v = beta;
    // the results are stored in E_prime and B_prime vectors
    double beta_dot_E = 0.0;
    double beta_dot_B = 0.0;
    for (int i = 0; i < 3; i++) {
        beta_dot_E += beta[i]*E_lab[i];
        beta_dot_B += beta[i]*B_lab[i];
    }
    double gamma =
        1./sqrt(1. - beta[0]*beta[0] - beta[1]*beta[1] - beta[2]*beta[2]);
    double beta_cross_E[3], beta_cross_B[3];
    cross_product(beta, E_lab, beta_cross_E);
    cross_product(beta, B_lab, beta_cross_B);
    double gamma_factor = gamma*gamma/(gamma + 1.);
    for (int i = 0; i < 3; i++) {
        E_prime[i] = (gamma*(E_lab[i] + beta_cross_B[i])
                      - gamma_factor*beta_dot_E*beta[i]);
        B_prime[i] = (gamma*(B_lab[i] - beta_cross_E[i])
                      - gamma_factor*beta_dot_B*beta[i]);
    }
}

void EM_fields::cross_product(double *a, double *b, double *c) {
    // this function calculates c = a x b
    c[0] = a[1]*b[2] - a[2]*b[1];
    c[1] = a[2]*b[0] - a[0]*b[2];
    c[2] = a[0]*b[1] - a[1]*b[0];
}
