// Copyright 2016 Chun Shen
#include <stdlib.h>
#include <omp.h>

#include <iostream>
#include <sstream>
#include <fstream>
#include <cmath>
#include <iomanip>
#include <string>

#include "./parameter.h"
#include "./EM_fields.h"
#include "./gauss_quadrature.h"

using namespace std;

EM_fields::EM_fields(ParameterReader* paraRdr_in) {
    initialization_status = 0;
    paraRdr = paraRdr_in;

    debug_flag = paraRdr->getVal("debug_flag");
    mode = paraRdr->getVal("mode");
    verbose_level = paraRdr->getVal("verbose_level");
    turn_on_bulk = paraRdr->getVal("turn_on_bulk");
    include_participant_contributions =
                        paraRdr->getVal("include_participant_contributions");
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
            (-(nucleon_density_grid_size-1)/2. + i)*nucleon_density_grid_dx);
        nucleon_density_grid_y_array[i] = (
            (-(nucleon_density_grid_size-1)/2. + i)*nucleon_density_grid_dx);
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
    eta_grid = new double[n_eta];
    sinh_eta_array = new double[n_eta];
    cosh_eta_array = new double[n_eta];
    double deta = 1.0;
    if (n_eta > 1) {
        deta = 2.*beam_rapidity*0.99/(n_eta - 1.);
        for (int i = 0; i < n_eta; i++) {
            eta_grid[i] = - beam_rapidity*0.99 + i*deta;
            sinh_eta_array[i] = sinh(eta_grid[i]);
            cosh_eta_array[i] = cosh(eta_grid[i]);
        }
    } else {
        eta_grid[0] = 0.0;
        sinh_eta_array[0] = sinh(eta_grid[0]);
        cosh_eta_array[0] = cosh(eta_grid[0]);
    }

    read_in_densities("./results");

    if (mode == 0) {
        set_4d_grid_points();
    } else if (mode == 1) {
        read_in_freezeout_surface_points_VISH2p1("./results/surface.dat",
                                                 "./results/decdat2.dat");
    } else if (mode == 2) {
        set_tau_grid_points(0.0, 0.0, 0.0);
    } else if (mode == 3) {
        read_in_freezeout_surface_points_VISH2p1_boost_invariant(
                                                    "./results/surface.dat");
    } else if (mode == 4) {
        read_in_freezeout_surface_points_MUSIC("./results/surface.dat");
    } else if (mode == -1) {
        read_in_freezeout_surface_points_Gubser("./results/surface.dat");
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
    ostringstream spectator_2_filename;
    if (mode == -1) {
        spectator_1_filename << path
                             << "/spectator_density_A_disk.dat";
        spectator_2_filename << path
                             << "/spectator_density_B_disk.dat";
    } else {
        spectator_1_filename << path
                             << "/spectator_density_A_fromSd_order_2.dat";
        spectator_2_filename << path
                             << "/spectator_density_B_fromSd_order_2.dat";
    }
    read_in_spectators_density(spectator_1_filename.str(),
                               spectator_2_filename.str());
    // participants
    if (include_participant_contributions == 1) {
        ostringstream participant_1_filename;
        participant_1_filename << path 
                               << "/nuclear_thickness_TA_fromSd_order_2.dat";
        ostringstream participant_2_filename;
        participant_2_filename << path 
                               << "/nuclear_thickness_TB_fromSd_order_2.dat";
        read_in_participant_density(participant_1_filename.str(), 
                                    participant_2_filename.str());
    }
}

void EM_fields::read_in_spectators_density(string filename_1,
                                           string filename_2) {
    if (verbose_level > 3) {
        cout << "read in spectator density ...";
    }
    ifstream spec1(filename_1.c_str());
    ifstream spec2(filename_2.c_str());
    if (!spec1.good()) {
        cout << "Error:EM_fields::read_in_spectators_density: "
             << "can not open file " << filename_1 << endl;
        exit(1);
    }
    if (!spec2.good()) {
        cout << "Error:EM_fields::read_in_spectators_density: "
             << "can not open file " << filename_2 << endl;
        exit(1);
    }

    for (int i = 0; i < nucleon_density_grid_size; i++) {
        for (int j = 0; j < nucleon_density_grid_size; j++) {
            spec1 >> spectator_density_1[i][j];
            spec2 >> spectator_density_2[i][j];
        }
    }

    spec1.close();
    spec2.close();
    if (verbose_level > 3) {
        cout << " done!" << endl;
    }
}

void EM_fields::read_in_participant_density(string filename_1,
                                            string filename_2) {
    if (verbose_level > 3) {
        cout << "read in participant density ...";
    }
    ifstream part1(filename_1.c_str());
    ifstream part2(filename_2.c_str());
    if (!part1.good()) {
        cout << "Error:EM_fields::read_in_participant_density: "
             << "can not open file " << filename_1 << endl;
        exit(1);
    }
    if (!part2.good()) {
        cout << "Error:EM_fields::read_in_participant_density: "
             << "can not open file " << filename_2 << endl;
        exit(1);
    }

    for (int i = 0; i < nucleon_density_grid_size; i++) {
        for (int j = 0; j < nucleon_density_grid_size; j++) {
            part1 >> participant_density_1[i][j];
            part2 >> participant_density_2[i][j];
        }
    }

    part1.close();
    part2.close();
    if (verbose_level > 3) {
        cout << " done!" << endl;
    }
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
    if (verbose_level > 1) {
        cout << "number of freeze-out cells: "
             << EM_fields_array_length << endl;
    }
}

void EM_fields::set_4d_grid_points() {
    cell_list.clear();
    double EM_fields_grid_size = 20.0;
    double EM_fields_grid_dx = 0.5;
    double EM_fields_grid_neta = 41;
    double EM_fields_grid_ntau = 11;
    double EM_fields_grid_tau_max = 5.0;
    int number_of_points =
                static_cast<int>(EM_fields_grid_size/EM_fields_grid_dx) + 1;
    double EM_fields_grid_deta = 2.*spectator_rap/(EM_fields_grid_neta - 1);
    double EM_fields_grid_dtau =
                EM_fields_grid_tau_max/(EM_fields_grid_ntau - 1);
    for (int l = 0; l < EM_fields_grid_ntau; l++) {
        double tau_local = 0.0 + l*EM_fields_grid_dtau;
        for (int k = 0; k < EM_fields_grid_neta; k++) {
            double eta_local = -spectator_rap + k*EM_fields_grid_deta;
            for (int i = 0; i < number_of_points; i++) {
                double x_local = - EM_fields_grid_size/2. + i*EM_fields_grid_dx;
                for (int j = 0; j < number_of_points; j++) {
                    double y_local = - EM_fields_grid_size/2. + j*EM_fields_grid_dx;
                    fluidCell cell_local;
                    cell_local.tau = tau_local;
                    cell_local.x = x_local;
                    cell_local.y = y_local;
                    cell_local.eta = eta_local;
                    cell_local.mu_m = M_PI/2.*sqrt(6*M_PI)*0.2*0.2;  // GeV^2
                    cell_local.beta.x = 0.0;
                    cell_local.beta.y = 0.0;
                    cell_local.beta.z = tanh(eta_local);
                    cell_list.push_back(cell_local);
                }
            }
        }
    }
    EM_fields_array_length = cell_list.size();
    if (verbose_level > 1) {
        cout << "number of freeze-out cells: "
             << EM_fields_array_length << endl;
    }
}

void EM_fields::read_in_freezeout_surface_points_VISH2p1(string filename1,
                                                         string filename2) {
    // this function reads in the freeze out surface points from a text file
    ifstream FOsurf(filename1.c_str());
    ifstream decdat(filename2.c_str());
    if (verbose_level > 1) {
        cout << "read in freeze-out surface points from VISH2+1 outputs ...";
    }
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
    if (verbose_level > 1) {
        cout << " done!" << endl;
    }
    EM_fields_array_length = cell_list.size();
    if (verbose_level > 1) {
        cout << "number of freeze-out cells: "
             << EM_fields_array_length << endl;
    }
}

void EM_fields::read_in_freezeout_surface_points_Gubser(string filename) {
    // this function reads in the freeze out surface points from a text file
    ifstream FOsurf(filename.c_str());
    if (verbose_level > 1) {
        cout << "read in freeze-out surface points from Gubser ...";
    }
    if (!FOsurf.good()) {
        cout << "Error:EM_fields::"
             << "read_in_freezeout_surface_points_Gubser:"
             << "can not open file: " << filename << endl;
        exit(1);
    }

    // read in freeze-out surface positions
    string input;
    double tau_local, x_local, y_local;
    double u_tau_local, u_x_local, u_y_local;
    double T_local;
    getline(FOsurf, input, '\n');  // read in header
    getline(FOsurf, input, '\n');
    while (!FOsurf.eof()) {
        stringstream ss(input);
        ss >> x_local >> tau_local >> u_tau_local >> u_x_local;
        y_local = 0.0;
        u_y_local = 0.0;
        T_local = 0.255;  // GeV
        fluidCell cell_local;
        cell_local.mu_m = M_PI/2.*sqrt(6*M_PI)*T_local*T_local;  // GeV^2
        if (cell_local.mu_m < 1e-5) {     // mu_m is too small
            cout << cell_local.mu_m << "  " << T_local << endl;
            exit(1);
        }
        cell_local.eta = 0.0;
        cell_local.tau = tau_local;
        cell_local.x = x_local;
        cell_local.y = y_local;

        // compute fluid velocity in t-xyz coordinate
        double u_t_local = u_tau_local;
        double u_z_local = 0.0;
        cell_local.beta.x = u_x_local/u_t_local;
        cell_local.beta.y = u_y_local/u_t_local;
        cell_local.beta.z = u_z_local/u_t_local;

        // push back the fluid cell into the cell list
        cell_list.push_back(cell_local);
        getline(FOsurf, input, '\n');
    }
    FOsurf.close();
    if (verbose_level > 1) {
        cout << " done!" << endl;
    }
    EM_fields_array_length = cell_list.size();
    if (verbose_level > 1) {
        cout << "number of freeze-out cells: "
             << EM_fields_array_length << endl;
    }
}

void EM_fields::read_in_freezeout_surface_points_VISH2p1_boost_invariant(
                                                            string filename) {
    // this function reads in the freeze out surface points from a text file
    ifstream FOsurf(filename.c_str());
    if (verbose_level > 1) {
        cout << "read in freeze-out surface points from VISH2+1 "
             << "boost invariant outputs ...";
    }
    if (!FOsurf.good()) {
        cout << "Error:EM_fields::"
             << "read_in_freezeout_surface_points_VISH2p1_boost_invariant:"
             << "can not open file: " << filename << endl;
        exit(1);
    }

    // read in freeze-out surface positions
    double dummy;
    string input;
    double tau_local, x_local, y_local;
    double u_tau_local, u_x_local, u_y_local;
    double T_local;
    getline(FOsurf, input, '\n');
    while (!FOsurf.eof()) {
        stringstream ss(input);
        ss >> tau_local >> x_local >> y_local >> dummy;  // eta_s = 0.0
        ss >> dummy >> dummy >> dummy >> dummy;   // skip surface vector da_mu
        // read in flow velocity
        ss >> dummy >> u_x_local >> u_y_local >> dummy;  // u_eta = 0.0
        u_tau_local = sqrt(1. + u_x_local*u_x_local + u_y_local*u_y_local);
        ss >> dummy >> dummy >> T_local;
        // the rest information is discarded
        for (int i = 0; i < n_eta; i++) {
            fluidCell cell_local;
            cell_local.mu_m = M_PI/2.*sqrt(6*M_PI)*T_local*T_local;  // GeV^2
            if (cell_local.mu_m < 1e-5) {     // mu_m is too small
                cout << cell_local.mu_m << "  " << T_local << endl;
                exit(1);
            }
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
        getline(FOsurf, input, '\n');
    }
    FOsurf.close();
    if (verbose_level > 1) {
        cout << " done!" << endl;
    }
    EM_fields_array_length = cell_list.size();
    if (verbose_level > 1) {
        cout << "number of freeze-out cells: "
             << EM_fields_array_length << endl;
    }
}

void EM_fields::read_in_freezeout_surface_points_MUSIC(string filename) {
    // this function reads in the freeze out surface points from a text file
    ifstream FOsurf(filename.c_str());
    if (verbose_level > 1) {
        cout << "read in freeze-out surface points from MUSIC "
             << "(3+1)-d outputs ...";
    }
    if (!FOsurf.good()) {
        cout << "Error:EM_fields::"
             << "read_in_freezeout_surface_points_MUSIC:"
             << "can not open file: " << filename << endl;
        exit(1);
    }

    // read in freeze-out surface positions
    double dummy;
    string input;
    double tau_local, x_local, y_local, eta_s_local;
    double u_tau_local, u_x_local, u_y_local, u_eta_local;
    double T_local;
    getline(FOsurf, input, '\n');
    while (!FOsurf.eof()) {
        stringstream ss(input);
        ss >> tau_local >> x_local >> y_local >> eta_s_local;
        ss >> dummy >> dummy >> dummy >> dummy;
        // read in flow velocity
        // here u^eta = tau*u^eta
        ss >> dummy >> u_x_local >> u_y_local >> u_eta_local;
        u_tau_local = sqrt(1. + u_x_local*u_x_local + u_y_local*u_y_local
                           + u_eta_local*u_eta_local);
        ss >> dummy >> T_local;
        // the rest information is discarded
        fluidCell cell_local;
        cell_local.mu_m = M_PI/2.*sqrt(6*M_PI)*T_local*T_local;  // GeV^2
        if (cell_local.mu_m < 1e-5) {     // mu_m is too small
            cout << cell_local.mu_m << "  " << T_local << endl;
            exit(1);
        }
        cell_local.eta = eta_s_local;
        cell_local.tau = tau_local;
        cell_local.x = x_local;
        cell_local.y = y_local;

        // compute fluid velocity in t-xyz coordinate
        double cosh_eta_s = cosh(eta_s_local);
        double sinh_eta_s = sinh(eta_s_local);
        double u_t_local = u_tau_local*cosh_eta_s + u_eta_local*sinh_eta_s;
        double u_z_local = u_tau_local*sinh_eta_s + u_eta_local*cosh_eta_s;
        cell_local.beta.x = u_x_local/u_t_local;
        cell_local.beta.y = u_y_local/u_t_local;
        cell_local.beta.z = u_z_local/u_t_local;

        // push back the fluid cell into the cell list
        cell_list.push_back(cell_local);
        getline(FOsurf, input, '\n');
    }
    FOsurf.close();
    if (verbose_level > 1) {
        cout << " done!" << endl;
    }
    EM_fields_array_length = cell_list.size();
    if (verbose_level > 1) {
        cout << "number of freeze-out cells: "
             << EM_fields_array_length << endl;
    }
}

void EM_fields::calculate_EM_fields() {
    int i_array;
    int count = 0;
    #pragma omp parallel private(i_array, count)
    {
    if (omp_get_thread_num() == 0) {
        cout << "computing EM fields with " << omp_get_num_threads()
             << " cpu cores..." << endl;
    }
    #pragma omp for
    for (i_array = 0; i_array < EM_fields_array_length; i_array++) {
        double sigma = 0.023;       // electric conductivity [fm^-1]
        double cosh_spectator_rap = cosh(spectator_rap);
        double sinh_spectator_rap = sinh(spectator_rap);

        double participant_coeff_a = 0.5;
        double participant_rapidity_envelop_coeff =
            participant_coeff_a/(2.*sinh(participant_coeff_a*spectator_rap));
        int participant_rapidity_integral_ny = 50;
        double *participant_rap_inte_y_array =
                                new double[participant_rapidity_integral_ny];
        double *participant_rap_inte_weight_array =
                                new double[participant_rapidity_integral_ny];
        gauss_quadrature(participant_rapidity_integral_ny, 1, 0.0, 0.0,
                         -spectator_rap, spectator_rap,
                         participant_rap_inte_y_array,
                         participant_rap_inte_weight_array);

        double dx_sq = nucleon_density_grid_dx*nucleon_density_grid_dx;

        double field_x = cell_list[i_array].x;
        double field_y = cell_list[i_array].y;
        double field_tau = cell_list[i_array].tau;
        double field_eta = cell_list[i_array].eta;
        double temp_sum_Ex_spectator = 0.0e0;
        double temp_sum_Ey_spectator = 0.0e0;
        double temp_sum_Ez_spectator = 0.0e0;
        double temp_sum_Bx_spectator = 0.0e0;
        double temp_sum_By_spectator = 0.0e0;

        double z_local_spectator_1 = field_tau*sinh(spectator_rap - field_eta);
        double z_local_spectator_2 = (
                                field_tau*sinh(-spectator_rap - field_eta));
        double z_local_spectator_1_sq = (z_local_spectator_1
                                         *z_local_spectator_1);
        double z_local_spectator_2_sq = (z_local_spectator_2
                                         *z_local_spectator_2);

        for (int i = 0; i < nucleon_density_grid_size; i++) {
            double grid_x = nucleon_density_grid_x_array[i];
            for (int j = 0; j < nucleon_density_grid_size; j++) {
                double grid_y = nucleon_density_grid_y_array[j];
                double x_local = field_x - grid_x;
                double y_local = field_y - grid_y;
                double r_perp_local_sq = x_local*x_local + y_local*y_local;
                double Delta_1 = sqrt(r_perp_local_sq
                                      + z_local_spectator_1_sq);
                double Delta_1_cubic = Delta_1*Delta_1*Delta_1;
                double Delta_2 = sqrt(r_perp_local_sq
                                      + z_local_spectator_2_sq);
                double Delta_2_cubic = Delta_2*Delta_2*Delta_2;
                double A_1 = (sigma/2.*(z_local_spectator_1 - Delta_1)
                                      *sinh_spectator_rap);
                double A_2 = (sigma/2.*(z_local_spectator_2 + Delta_2)
                                      *(-sinh_spectator_rap));
                double exp_A_1 = exp(A_1);
                double exp_A_2 = exp(A_2);
                double common_integrand_E = (
                    spectator_density_1[i][j]/(Delta_1_cubic + 1e-15)
                      *(sigma/2.*sinh_spectator_rap*Delta_1 + 1.)*exp_A_1
                    + spectator_density_2[i][j]/(Delta_2_cubic + 1e-15)
                      *(sigma/2.*sinh_spectator_rap*Delta_2 + 1.)*exp_A_2
                );
                double common_integrand_B = (
                    spectator_density_1[i][j]/(Delta_1_cubic + 1e-15)
                    *(sigma/2.*sinh_spectator_rap*Delta_1 + 1.)*exp_A_1
                    - spectator_density_2[i][j]/(Delta_2_cubic + 1e-15)
                      *(sigma/2.*sinh_spectator_rap*Delta_2 + 1.)*exp_A_2
                );

                double Ex_integrand = x_local*common_integrand_E;
                double Ey_integrand = y_local*common_integrand_E;
                double Bx_integrand = -y_local*common_integrand_B;
                double By_integrand = x_local*common_integrand_B;
                
                temp_sum_Ex_spectator += Ex_integrand;
                temp_sum_Ey_spectator += Ey_integrand;
                temp_sum_Bx_spectator += Bx_integrand;
                temp_sum_By_spectator += By_integrand;
            }
        }
        
        // compute contribution from participants
        double temp_sum_Ex_participant = 0.0e0;
        double temp_sum_Ey_participant = 0.0e0;
        double temp_sum_Ez_participant = 0.0e0;
        double temp_sum_Bx_participant = 0.0e0;
        double temp_sum_By_participant = 0.0e0;
        
        if (include_participant_contributions == 1) {
            for (int k = 0; k < participant_rapidity_integral_ny; k++) {
                double rap_local = participant_rap_inte_y_array[k];
                double sinh_participant_rap = sinh(rap_local);
                double cosh_participant_rap = cosh(rap_local);

                double exp_participant_rap_1 =
                                        exp(participant_coeff_a*rap_local);
                double exp_participant_rap_2 = exp_participant_rap_1;
                double z_local_participant_1 =
                                    field_tau*sinh(rap_local - field_eta);
                double z_local_participant_2 = (
                                    field_tau*sinh(-rap_local - field_eta));
                double z_local_participant_1_sq = (z_local_participant_1
                                                   *z_local_participant_1);
                double z_local_participant_2_sq = (z_local_participant_2
                                                   *z_local_participant_2);

                double Ex_integrand = 0.0;
                double Ey_integrand = 0.0;
                double Bx_integrand = 0.0;
                double By_integrand = 0.0;
                for (int i = 0; i < nucleon_density_grid_size; i++) {
                    double grid_x = nucleon_density_grid_x_array[i];
                    for (int j = 0; j < nucleon_density_grid_size; j++) {
                        double grid_y = nucleon_density_grid_y_array[j];
                        double x_local = field_x - grid_x;
                        double y_local = field_y - grid_y;
                        double r_perp_local_sq = (
                                        x_local*x_local + y_local*y_local);
                        double Delta_1 = sqrt(r_perp_local_sq
                                              + z_local_participant_1_sq);
                        double Delta_1_cubic = Delta_1*Delta_1*Delta_1;
                        double Delta_2 = sqrt(r_perp_local_sq
                                              + z_local_participant_2_sq);
                        double Delta_2_cubic = Delta_2*Delta_2*Delta_2;
                        double A_1 = (sigma/2.
                                *(z_local_participant_1*sinh_participant_rap
                                  - fabs(sinh_participant_rap)*Delta_1));
                        double A_2 = (sigma/2.
                                *(z_local_participant_2*(-sinh_participant_rap)
                                  - fabs(-sinh_participant_rap)*Delta_2));
                        double exp_A_1 = exp(A_1);
                        double exp_A_2 = exp(A_2);
                        double common_integrand_E = (
                            (participant_density_1[i][j]
                             /(Delta_1_cubic + 1e-15)
                             *(sigma/2.*fabs(sinh_participant_rap)*Delta_1 + 1.)
                             *exp_A_1)*exp_participant_rap_1
                          + (participant_density_2[i][j]
                             /(Delta_2_cubic + 1e-15)
                             *(sigma/2.*fabs(sinh_participant_rap)*Delta_2 + 1.)
                             *exp_A_2)*exp_participant_rap_2);
                        double common_integrand_B = (
                            (participant_density_1[i][j]
                             /(Delta_1_cubic + 1e-15)
                             *(sigma/2.*fabs(sinh_participant_rap)*Delta_1 + 1.)
                             *exp_A_1)*exp_participant_rap_1
                          - (participant_density_2[i][j]
                             /(Delta_2_cubic + 1e-15)
                             *(sigma/2.*fabs(sinh_participant_rap)*Delta_2 + 1.)
                             *exp_A_2)*exp_participant_rap_2);

                        Ex_integrand += x_local*common_integrand_E;
                        Ey_integrand += y_local*common_integrand_E;
                        Bx_integrand += -y_local*common_integrand_B;
                        By_integrand += x_local*common_integrand_B;
                    }
                }
                temp_sum_Ex_participant += (Ex_integrand*cosh_participant_rap
                                        *participant_rap_inte_weight_array[k]);
                temp_sum_Ey_participant += (Ey_integrand*cosh_participant_rap
                                        *participant_rap_inte_weight_array[k]);
                temp_sum_Bx_participant += (Bx_integrand*sinh_participant_rap
                                        *participant_rap_inte_weight_array[k]);
                temp_sum_By_participant += (By_integrand*sinh_participant_rap
                                        *participant_rap_inte_weight_array[k]);
            }
        }

        cell_list[i_array].E_lab.x = (charge_fraction*alpha_EM
            *(temp_sum_Ex_spectator*cosh_spectator_rap
              + temp_sum_Ex_participant*participant_rapidity_envelop_coeff
             )*dx_sq);
        cell_list[i_array].E_lab.y = (charge_fraction*alpha_EM
            *(temp_sum_Ey_spectator*cosh_spectator_rap
              + temp_sum_Ey_participant*participant_rapidity_envelop_coeff
             )*dx_sq);
        cell_list[i_array].E_lab.z = (charge_fraction*alpha_EM
            *(temp_sum_Ez_spectator
              + temp_sum_Ez_participant*participant_rapidity_envelop_coeff
             )*dx_sq);
        cell_list[i_array].B_lab.x = (charge_fraction*alpha_EM
            *(temp_sum_Bx_spectator*sinh_spectator_rap
              + temp_sum_Bx_participant*participant_rapidity_envelop_coeff
             )*dx_sq);
        cell_list[i_array].B_lab.y = (charge_fraction*alpha_EM
            *(temp_sum_By_spectator*sinh_spectator_rap
              + temp_sum_By_participant*participant_rapidity_envelop_coeff
             )*dx_sq);
        cell_list[i_array].B_lab.z = 0.0;

        // convert units to [GeV^2]
        cell_list[i_array].E_lab.x *= hbarCsq;
        cell_list[i_array].E_lab.y *= hbarCsq;
        cell_list[i_array].E_lab.z *= hbarCsq;
        cell_list[i_array].B_lab.x *= hbarCsq;
        cell_list[i_array].B_lab.y *= hbarCsq;
        cell_list[i_array].B_lab.z *= hbarCsq;

        if (verbose_level > 3) {
            if (omp_get_thread_num() == 0) {
                count++;
                int total_num_cells = static_cast<int>(EM_fields_array_length
                                                       /omp_get_num_threads());
                if (count % static_cast<int>(total_num_cells/10) == 0) {
                    cout << "computing EM fields: " << setprecision(3)
                         << (static_cast<double>(count)
                             /static_cast<double>(total_num_cells)*100)
                         << "\% done." << endl;
                }
            }
        }
        // clean up
        delete[] participant_rap_inte_y_array;
        delete[] participant_rap_inte_weight_array;
    }
    #pragma omp barrier
    }
    return;
}

void EM_fields::calculate_EM_fields_no_electric_conductivity() {
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
            hbarCsq*charge_fraction*alpha_EM
            *cosh_spectator_rap*temp_sum_Ex_spectator*dx_sq);
        cell_list[i_array].E_lab.y = (
            hbarCsq*charge_fraction*alpha_EM
            *cosh_spectator_rap*temp_sum_Ey_spectator*dx_sq);
        cell_list[i_array].E_lab.z = (
            hbarCsq*charge_fraction*alpha_EM*temp_sum_Ez_spectator*dx_sq);
        cell_list[i_array].B_lab.x = (
            hbarCsq*charge_fraction*alpha_EM
            *((-sinh_spectator_rap)*temp_sum_Bx_spectator)*dx_sq);
        cell_list[i_array].B_lab.y = (
            hbarCsq*charge_fraction*alpha_EM
            *(sinh_spectator_rap*temp_sum_By_spectator)*dx_sq);
        cell_list[i_array].B_lab.z = 0.0;

        if (verbose_level > 3) {
            if (i_array % static_cast<int>(EM_fields_array_length/10) == 0) {
                cout << "computing EM fields: " << setprecision(3)
                     << (static_cast<double>(i_array)
                         /static_cast<double>(EM_fields_array_length)*100)
                     << "\% done." << endl;
            }
        }
    }
    return;
}

void EM_fields::output_EM_fields(string filename) {
    // this function outputs the computed E and B fields to a text file
    ofstream output_file(filename.c_str());
    // write a header first
    if (mode == -1) {
        output_file << "# tau[fm]  x[fm]  y[fm]  eta  "
                    << "E_x[1/fm^2]  E_y[1/fm^2]  E_z[1/fm^2]  "
                    << "B_x[1/fm^2]  B_y[1/fm^2]  B_z[1/fm^2]" << endl;
        double unit_convert = 1./(hbarCsq*sqrt(alpha_EM*4*M_PI));
        for (int i = 0; i < EM_fields_array_length; i++) {
            output_file << scientific << setprecision(8) << setw(15)
                        << cell_list[i].tau << "   " << cell_list[i].x << "   "
                        << cell_list[i].y << "   " << cell_list[i].eta << "   "
                        << cell_list[i].E_lab.x*unit_convert << "   "
                        << cell_list[i].E_lab.y*unit_convert << "   "
                        << cell_list[i].E_lab.z*unit_convert << "   "
                        << cell_list[i].B_lab.x*unit_convert << "   "
                        << cell_list[i].B_lab.y*unit_convert << "   "
                        << cell_list[i].B_lab.z*unit_convert << endl;
        }
    } else {
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
    }
    output_file.close();
    return;
}

void EM_fields::output_surface_file_with_drifting_velocity(string filename) {
    // this function outputs hypersurface file with drifting velocity
    // the format of the hypersurface file is compatible with MUSIC
    ofstream output_file(filename.c_str());
    if (mode == 0) {
        for (int i = 0; i < EM_fields_array_length; i++) {
            output_file << scientific << setprecision(8) << setw(15)
                        << cell_list[i].tau << "  "
                        << cell_list[i].x << "  "
                        << cell_list[i].y << "  "
                        << cell_list[i].eta << "  "
                        << cell_list[i].drift_u_plus.tau << "  "
                        << cell_list[i].drift_u_plus.x << "  "
                        << cell_list[i].drift_u_plus.y << "  "
                        << cell_list[i].drift_u_plus.eta << "  "
                        << cell_list[i].drift_u_minus.tau << "  "
                        << cell_list[i].drift_u_minus.x << "  "
                        << cell_list[i].drift_u_minus.y << "  "
                        << cell_list[i].drift_u_minus.eta << "  "
                        << cell_list[i].drift_u_plus_2.tau << "  "
                        << cell_list[i].drift_u_plus_2.x << "  "
                        << cell_list[i].drift_u_plus_2.y << "  "
                        << cell_list[i].drift_u_plus_2.eta << "  "
                        << cell_list[i].drift_u_minus_2.tau << "  "
                        << cell_list[i].drift_u_minus_2.x << "  "
                        << cell_list[i].drift_u_minus_2.y << "  "
                        << cell_list[i].drift_u_minus_2.eta << endl;
        }
    } else if (mode == 1) {    // read in mode is from VISH2+1
        ifstream decdat("./results/decdat2.dat");
        string input;
        double dummy;
        double da0, da1, da2;
        double Edec, Tdec, muB, Pdec;
        double pi00, pi01, pi02, pi11, pi12, pi22, pi33;
        double bulkPi;
        int idx = 0;
        double deta = cell_list[1].eta - cell_list[0].eta;
        for (int i = 0; i < EM_fields_array_length/n_eta; i++) {
            // read in other hyper-surface information
            getline(decdat, input, '\n');
            stringstream ss(input);
            ss >> dummy >> da0 >> da1 >> da2;  // read in da_mu
            double da3 = 0.0;
            ss >> dummy >> dummy;              // pipe vx and vy to dummy
            ss >> Edec >> dummy >> Tdec >> muB >> dummy >> Pdec;
            double e_plus_P_over_T = (Edec + Pdec)/Tdec;
            ss >> pi33 >> pi00 >> pi01 >> pi02 >> pi11 >> pi12 >> pi22;
            double pi03 = 0.0;
            double pi13 = 0.0;
            double pi23 = 0.0;
            ss >> bulkPi;
            for (int j = 0; j < n_eta; j++) {
                double u_t = (
                    1./sqrt(1. - cell_list[idx].beta.x*cell_list[idx].beta.x
                               - cell_list[idx].beta.y*cell_list[idx].beta.y
                               - cell_list[idx].beta.z*cell_list[idx].beta.z));
                double u_x = cell_list[idx].beta.x*u_t;
                double u_y = cell_list[idx].beta.y*u_t;
                double u_z = cell_list[idx].beta.z*u_t;
                double u_tau = (u_t*cosh(cell_list[idx].eta)
                                - u_z*sinh(cell_list[idx].eta));
                double u_eta = 0.0;            // for boost-invariant medium
                output_file << scientific << setprecision(8) << setw(15)
                            << cell_list[idx].tau << "  "
                            << cell_list[idx].x << "  "
                            << cell_list[idx].y << "  "
                            << cell_list[idx].eta << "  "
                            << da0*deta << "  " << da1*deta << "  "
                            << da2*deta << "  " << da3*deta << "  "
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
                // output drifting velocity at the end
                output_file << scientific << setprecision(8) << setw(15)
                            << cell_list[idx].drift_u_plus.tau << "  "
                            << cell_list[idx].drift_u_plus.x << "  "
                            << cell_list[idx].drift_u_plus.y << "  "
                            << cell_list[idx].drift_u_plus.eta << "  "
                            << cell_list[idx].drift_u_minus.tau << "  "
                            << cell_list[idx].drift_u_minus.x << "  "
                            << cell_list[idx].drift_u_minus.y << "  "
                            << cell_list[idx].drift_u_minus.eta << "  "
                            << cell_list[idx].drift_u_plus_2.tau << "  "
                            << cell_list[idx].drift_u_plus_2.x << "  "
                            << cell_list[idx].drift_u_plus_2.y << "  "
                            << cell_list[idx].drift_u_plus_2.eta << "  "
                            << cell_list[idx].drift_u_minus_2.tau << "  "
                            << cell_list[idx].drift_u_minus_2.x << "  "
                            << cell_list[idx].drift_u_minus_2.y << "  "
                            << cell_list[idx].drift_u_minus_2.eta << "  ";
                output_file << endl;
                idx++;
            }
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
        int idx = 0;
        double deta = cell_list[1].eta - cell_list[0].eta;
        for (int i = 0; i < EM_fields_array_length/n_eta; i++) {
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
            for (int j = 0; j < n_eta; j++) {
                output_file << scientific << setprecision(8) << setw(15)
                            << cell_list[idx].tau << "  "
                            << cell_list[idx].x << "  "
                            << cell_list[idx].y << "  "
                            << cell_list[idx].eta << "  "
                            << da0*deta << "  " << da1*deta << "  "
                            << da2*deta << "  " << da3*deta << "  "
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
                            << cell_list[idx].drift_u_plus.tau << "  "
                            << cell_list[idx].drift_u_plus.x << "  "
                            << cell_list[idx].drift_u_plus.y << "  "
                            << cell_list[idx].drift_u_plus.eta << "  "
                            << cell_list[idx].drift_u_minus.tau << "  "
                            << cell_list[idx].drift_u_minus.x << "  "
                            << cell_list[idx].drift_u_minus.y << "  "
                            << cell_list[idx].drift_u_minus.eta << "  "
                            << cell_list[idx].drift_u_plus_2.tau << "  "
                            << cell_list[idx].drift_u_plus_2.x << "  "
                            << cell_list[idx].drift_u_plus_2.y << "  "
                            << cell_list[idx].drift_u_plus_2.eta << "  "
                            << cell_list[idx].drift_u_minus_2.tau << "  "
                            << cell_list[idx].drift_u_minus_2.x << "  "
                            << cell_list[idx].drift_u_minus_2.y << "  "
                            << cell_list[idx].drift_u_minus_2.eta;
                output_file << endl;
                idx++;
            }
        }
        decdat.close();
    } else if (mode == 4) {
        ifstream decdat("./results/surface.dat");
        string input;
        for (int i = 0; i < EM_fields_array_length; i++) {
            getline(decdat, input, '\n');
            output_file << input;
            output_file << " " << scientific << setprecision(8) << setw(15)
                        << cell_list[i].drift_u_plus.tau << "  "
                        << cell_list[i].drift_u_plus.x << "  "
                        << cell_list[i].drift_u_plus.y << "  "
                        << cell_list[i].drift_u_plus.eta << "  "
                        << cell_list[i].drift_u_minus.tau << "  "
                        << cell_list[i].drift_u_minus.x << "  "
                        << cell_list[i].drift_u_minus.y << "  "
                        << cell_list[i].drift_u_minus.eta << "  "
                        << cell_list[i].drift_u_plus_2.tau << "  "
                        << cell_list[i].drift_u_plus_2.x << "  "
                        << cell_list[i].drift_u_plus_2.y << "  "
                        << cell_list[i].drift_u_plus_2.eta << "  "
                        << cell_list[i].drift_u_minus_2.tau << "  "
                        << cell_list[i].drift_u_minus_2.x << "  "
                        << cell_list[i].drift_u_minus_2.y << "  "
                        << cell_list[i].drift_u_minus_2.eta << endl;
        }
        decdat.close();
    } else if (mode == -1) {
        ifstream decdat("./results/surface.dat");
        string input;
        // print out the header
        getline(decdat, input, '\n');
        output_file << input << endl;
        for (int i = 0; i < EM_fields_array_length; i++) {
            getline(decdat, input, '\n');
            output_file << input;
            output_file << " " << scientific << setprecision(8) << setw(15)
                        << cell_list[i].drift_u_plus.tau << "  "
                        << cell_list[i].drift_u_plus.x << "  "
                        << cell_list[i].drift_u_plus.y << "  "
                        << cell_list[i].drift_u_plus.eta << "  "
                        << cell_list[i].drift_u_minus.tau << "  "
                        << cell_list[i].drift_u_minus.x << "  "
                        << cell_list[i].drift_u_minus.y << "  "
                        << cell_list[i].drift_u_minus.eta << "  "
                        << cell_list[i].drift_u_plus_2.tau << "  "
                        << cell_list[i].drift_u_plus_2.x << "  "
                        << cell_list[i].drift_u_plus_2.y << "  "
                        << cell_list[i].drift_u_plus_2.eta << "  "
                        << cell_list[i].drift_u_minus_2.tau << "  "
                        << cell_list[i].drift_u_minus_2.x << "  "
                        << cell_list[i].drift_u_minus_2.y << "  "
                        << cell_list[i].drift_u_minus_2.eta << endl;
        }
        decdat.close();
    }
    output_file.close();
}

void EM_fields::calculate_charge_drifting_velocity() {
    // this function calculates the drifting velocity of the fluid cell
    // included by the local EM fields
    if (verbose_level > 1) {
        cout << "calculating the charge drifiting velocity ... " << endl;
    }

    // initialization
    double *E_lab = new double[3];
    double *B_lab = new double[3];
    double *beta = new double[3];
    double *E_lrf = new double[3];
    double *B_lrf = new double[3];

    double *drift_u_plus = new double[4];
    double *drift_u_minus = new double[4];
    double *drift_u_plus_2 = new double[4];
    double *drift_u_minus_2 = new double[4];

    double q_array[] = {1.0, -1.0, 2.0, -2.0};
    int q_array_length = sizeof(q_array)/sizeof(double);
    double **drift_v = new double* [q_array_length];
    for (int i = 0; i < q_array_length; i++) {
        drift_v[i] = new double[3];
    }

    ofstream check, check2;
    if (debug_flag == 1) {
        check.open("results/check_lrf_velocity.dat", ios::out);
        check2.open("results/check_lrf_EMfields.dat", ios::out);
        check << "#tau  x  y  eta  vx  vy  vz" << endl;
        check2 << "#tau  x  y  eta  Ex[1/fm^2]  Ey[1/fm^2]  Ez[1/fm^2]  "
               << "Bx[1/fm^2]  By[1/fm^2]  Bz[1/fm^2]"
               << endl;
    }
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
        // Lorentz_boost_EM_fields_tensor(E_lab, B_lab, beta, E_lrf, B_lrf);
        double unit_convert = 1./(hbarCsq*sqrt(alpha_EM*4*M_PI));
        if (debug_flag == 1) {
            check2 << scientific << setw(18) << setprecision(8)
                   << cell_list[i].tau << "  " << cell_list[i].x << "  "
                   << cell_list[i].y << "  " << cell_list[i].eta << "  "
                   << E_lrf[0]*unit_convert << "  "
                   << E_lrf[1]*unit_convert << "  "
                   << E_lrf[2]*unit_convert << "  "
                   << B_lrf[0]*unit_convert << "  "
                   << B_lrf[1]*unit_convert << "  "
                   << B_lrf[2]*unit_convert << endl;
        }

        // we calculate the drifting velocity in the local rest frame
        double mu_m = cell_list[i].mu_m;

        // solve v for moving charges
        for (int j = 0; j < q_array_length; j++) {
            double q = q_array[j];
            double qEx = q*E_lrf[0];
            double qEy = q*E_lrf[1];
            double qEz = q*E_lrf[2];
            double qBx = q*B_lrf[0];
            double qBy = q*B_lrf[1];
            double qBz = q*B_lrf[2];
            double denorm = (
                    1./(mu_m*(qBx*qBx + qBy*qBy + qBz*qBz) + mu_m*mu_m*mu_m));

            double delta_v_x = (
                    qEz*(qBx*qBz - qBy*mu_m) + qEy*(qBx*qBy + qBz*mu_m)
                    + qEx*(qBx*qBx + mu_m*mu_m))*denorm;
            double delta_v_y = (
                    qEz*(qBy*qBz + qBx*mu_m) + qEx*(qBx*qBy - qBz*mu_m)
                    + qEy*(qBy*qBy + mu_m*mu_m))*denorm;
            double delta_v_z = (
                    qEy*(qBy*qBz - qBx*mu_m) + qEx*(qBx*qBz + qBy*mu_m)
                    + qEz*(qBz*qBz + mu_m*mu_m))*denorm;
            // check the solutions
            double check_x = (mu_m*delta_v_x - qBz*delta_v_y + qBy*delta_v_z
                              - qEx);
            double check_y = (qBz*delta_v_x + mu_m*delta_v_y - qBx*delta_v_z
                              - qEy);
            double check_z = (-qBy*delta_v_x + qBx*delta_v_y + mu_m*delta_v_z
                              - qEz);
            if (fabs(check_x) > 1e-10) {
                cout << "Error:EM_fields::calculate_charge_drifting_velocity:"
                     << " drifting velocity is not correct!"
                     << "check_x = " << check_x << endl;
                exit(1);
            }
            if (fabs(check_y) > 1e-10) {
                cout << "Error:EM_fields::calculate_charge_drifting_velocity:"
                     << " drifting velocity is not correct!"
                     << "check_y = " << check_y << endl;
                exit(1);
            }
            if (fabs(check_z) > 1e-10) {
                cout << "Error:EM_fields::calculate_charge_drifting_velocity:"
                     << " drifting velocity is not correct!"
                     << "check_z = " << check_z << endl;
                exit(1);
            }

            double gamma = 1./sqrt(1. - delta_v_x*delta_v_x
                                   - delta_v_y*delta_v_y
                                   - delta_v_z*delta_v_z);
            if (isnan(gamma)) {
                cout << "Error:EM_fields:calculate_charge_drifting_velocity():"
                     << " drifting velocity is too large!" << endl;
                cout << "gamma = " << gamma << ", delta_v_x = " << delta_v_x
                     << ", delta_v_y = " << delta_v_y << ", delta_v_z = "
                     << delta_v_z << endl;
                cout << "denorm = " << denorm << endl;
                cout << "mu_m = " << mu_m << ", qEx = " << qEx
                     << ", qEy = " << qEy << ", qEz = " << qEz
                     << ", qBx = " << qBx << ", qBy = " << qBy
                     << ", qBz = " << qBz << endl;
                cout << "beta_x = " << beta[0] << ", beta_y = " << beta[1]
                     << "beta_z = " << beta[2] << endl;
                cout << "eE_lab_x = " << E_lab[0]
                     << ", eE_lab_y = " << E_lab[1]
                     << ", eE_lab_z = " << E_lab[2]
                     << ", eB_lab_x = " << B_lab[0]
                     << ", eB_lab_y = " << B_lab[1]
                     << ", eB_lab_z = " << B_lab[2] << endl;
                exit(1);
            }
            drift_v[j][0] = delta_v_x;
            drift_v[j][1] = delta_v_y;
            drift_v[j][2] = delta_v_z;
            if (debug_flag == 1) {
                if (j == 0) {
                    check << scientific << setw(18) << setprecision(8)
                          << cell_list[i].tau << "  " << cell_list[i].x << "  "
                          << cell_list[i].y << "  " << cell_list[i].eta << "  "
                          << delta_v_x << "  " << delta_v_y << "  "
                          << delta_v_z << endl;
                }
            }
        }

        for (int l = 1; l < 4; l++) {
            drift_u_plus[l] = drift_v[0][l-1];
            drift_u_minus[l] = drift_v[1][l-1];
            drift_u_plus_2[l] = drift_v[2][l-1];
            drift_u_minus_2[l] = drift_v[3][l-1];
        }
        drift_u_plus[0] = 1./sqrt(1. - drift_u_plus[1]*drift_u_plus[1]
                                  - drift_u_plus[2]*drift_u_plus[2]
                                  - drift_u_plus[3]*drift_u_plus[3]);
        if (isnan(drift_u_plus[0])) {
            cout << "EM_fields::calculate_charge_drifting_velocity:"
                 << "drift_u_plus[0] is nan!" << endl;
            cout << "vx = " << drift_u_plus[1] << ", vy = " << drift_u_plus[2]
                 << ", vz = " << drift_u_plus[3] << endl;
            cout << "v[0][x] = " << drift_v[0][0]
                 << ", v[0][y] = " << drift_v[0][1]
                 << ", v[0][z] = " << drift_v[0][2] << endl;
            exit(1);
        }
        drift_u_minus[0] = 1./sqrt(1. - drift_u_minus[1]*drift_u_minus[1]
                                   - drift_u_minus[2]*drift_u_minus[2]
                                   - drift_u_minus[3]*drift_u_minus[3]);
        if (isnan(drift_u_minus[0])) {
            cout << "EM_fields::calculate_charge_drifting_velocity:"
                 << "drift_u_minus[0] is nan!" << endl;
            cout << "vx = " << drift_u_minus[1]
                 << ", vy = " << drift_u_minus[2]
                 << ", vz = " << drift_u_minus[3] << endl;
            cout << "v[1][x] = " << drift_v[1][0]
                 << ", v[1][y] = " << drift_v[1][1]
                 << ", v[1][z] = " << drift_v[1][2] << endl;
            exit(1);
        }
        drift_u_plus_2[0] = 1./sqrt(1. - drift_u_plus_2[1]*drift_u_plus_2[1]
                                    - drift_u_plus_2[2]*drift_u_plus_2[2]
                                    - drift_u_plus_2[3]*drift_u_plus_2[3]);
        if (isnan(drift_u_plus_2[0])) {
            cout << "EM_fields::calculate_charge_drifting_velocity:"
                 << "drift_u_plus_2[0] is nan!" << endl;
            cout << "vx = " << drift_u_plus_2[1]
                 << ", vy = " << drift_u_plus_2[2]
                 << ", vz = " << drift_u_plus_2[3] << endl;
            cout << "v[2][x] = " << drift_v[2][0]
                 << ", v[2][y] = " << drift_v[2][1]
                 << ", v[2][z] = " << drift_v[2][2] << endl;
            exit(1);
        }
        drift_u_minus_2[0] = 1./sqrt(1. - drift_u_minus_2[1]*drift_u_minus_2[1]
                                        - drift_u_minus_2[2]*drift_u_minus_2[2]
                                        - drift_u_minus_2[3]*drift_u_minus_2[3]
                                    );
        if (isnan(drift_u_minus_2[0])) {
            cout << "EM_fields::calculate_charge_drifting_velocity:"
                 << "drift_u_minus_2[0] is nan!" << endl;
            cout << "vx = " << drift_u_minus_2[1]
                 << ", vy = " << drift_u_minus_2[2]
                 << ", vz = " << drift_u_minus_2[3] << endl;
            cout << "v[3][x] = " << drift_v[3][0]
                 << ", v[3][y] = " << drift_v[3][1]
                 << ", v[3][z] = " << drift_v[3][2] << endl;
            exit(1);
        }
        for (int l = 1; l < 4; l++) {
            drift_u_plus[l] *= drift_u_plus[0];
            drift_u_minus[l] *= drift_u_minus[0];
            drift_u_plus_2[l] *= drift_u_plus_2[0];
            drift_u_minus_2[l] *= drift_u_minus_2[0];
        }

        // finally we boost the delta v back to the lab frame
        for (int k = 0; k < 3; k++) {  // prepare the velocity
            beta[k] = - beta[k];
        }
        // lorentz_transform_vector_with_Lambda(drift_u_plus, beta);
        // lorentz_transform_vector_with_Lambda(drift_u_minus, beta);
        lorentz_transform_vector_in_place(drift_u_plus, beta);
        lorentz_transform_vector_in_place(drift_u_minus, beta);
        lorentz_transform_vector_in_place(drift_u_plus_2, beta);
        lorentz_transform_vector_in_place(drift_u_minus_2, beta);

        // transform to tau-eta coordinate with tilde{u}^eta = tau*u^eta
        double eta_s = cell_list[i].eta;
        double sinh_eta_s = sinh(eta_s);
        double cosh_eta_s = cosh(eta_s);
        double drift_u_plus_tau = (
                drift_u_plus[0]*cosh_eta_s - drift_u_plus[3]*sinh_eta_s);
        double drift_u_plus_eta = (
                - drift_u_plus[0]*sinh_eta_s + drift_u_plus[3]*cosh_eta_s);
        cell_list[i].drift_u_plus.tau = drift_u_plus_tau;
        cell_list[i].drift_u_plus.x = drift_u_plus[1];
        cell_list[i].drift_u_plus.y = drift_u_plus[2];
        cell_list[i].drift_u_plus.eta = drift_u_plus_eta;
        double drift_u_minus_tau = (
                drift_u_minus[0]*cosh_eta_s - drift_u_minus[3]*sinh_eta_s);
        double drift_u_minus_eta = (
                - drift_u_minus[0]*sinh_eta_s + drift_u_minus[3]*cosh_eta_s);
        cell_list[i].drift_u_minus.tau = drift_u_minus_tau;
        cell_list[i].drift_u_minus.x = drift_u_minus[1];
        cell_list[i].drift_u_minus.y = drift_u_minus[2];
        cell_list[i].drift_u_minus.eta = drift_u_minus_eta;
        double drift_u_plus_2_tau = (
                drift_u_plus_2[0]*cosh_eta_s - drift_u_plus_2[3]*sinh_eta_s);
        double drift_u_plus_2_eta = (
                - drift_u_plus_2[0]*sinh_eta_s + drift_u_plus_2[3]*cosh_eta_s);
        cell_list[i].drift_u_plus_2.tau = drift_u_plus_2_tau;
        cell_list[i].drift_u_plus_2.x = drift_u_plus_2[1];
        cell_list[i].drift_u_plus_2.y = drift_u_plus_2[2];
        cell_list[i].drift_u_plus_2.eta = drift_u_plus_2_eta;
        double drift_u_minus_2_tau = (drift_u_minus_2[0]*cosh_eta_s
                                      - drift_u_minus_2[3]*sinh_eta_s);
        double drift_u_minus_2_eta = (- drift_u_minus_2[0]*sinh_eta_s
                                      + drift_u_minus_2[3]*cosh_eta_s);
        cell_list[i].drift_u_minus_2.tau = drift_u_minus_2_tau;
        cell_list[i].drift_u_minus_2.x = drift_u_minus_2[1];
        cell_list[i].drift_u_minus_2.y = drift_u_minus_2[2];
        cell_list[i].drift_u_minus_2.eta = drift_u_minus_2_eta;
    }
    if (debug_flag == 1) {
        check.close();
        check2.close();
    }

    // clean up
    for (int i = 0; i < q_array_length; i++) {
        delete [] drift_v[i];
    }
    delete [] drift_v;
    delete [] drift_u_plus;
    delete [] drift_u_minus;
    delete [] drift_u_plus_2;
    delete [] drift_u_minus_2;
    delete [] E_lab;
    delete [] B_lab;
    delete [] E_lrf;
    delete [] B_lrf;
    delete [] beta;
}

void EM_fields::lorentz_transform_vector_in_place(double *u_mu, double *v) {
// boost u^mu with velocity v and store the boosted vector back in u^mu
// v is a 3 vector and u_mu is a 4 vector
    double v2 = v[0]*v[0] + v[1]*v[1] + v[2]*v[2];
    double vp = v[0]*u_mu[1] + v[1]*u_mu[2] + v[2]*u_mu[3];
    if (v2 > 1.) {
        v2 = 1. - 1e-12;
    }

    double gamma = 1./sqrt(1. - v2);
    double gamma_m_1 = gamma - 1.;

    double ene = u_mu[0];

    u_mu[0] = gamma*(ene - vp);
    for (int i = 1; i < 4; i++) {
        u_mu[i] = u_mu[i] + (gamma_m_1*vp/(v2+1e-15) - gamma*ene)*v[i-1];
        if (isnan(u_mu[i])) {
            cout << "Error:lorentz_transform_vector_in_place: u is nan"
                 << endl;
            cout << "gamma-1=" << gamma_m_1 << ", vp=" << vp
                 << ", v2=" << v2 << ", ene=" << ene << ", v=" << v[i-1]
                 << endl;
            exit(1);
        }
    }
}

void EM_fields::lorentz_transform_vector_with_Lambda(double *u_mu,
                                                     double *beta) {
    // boost u^mu with velocity beta and store the boost vectored back in u^mu
    // beta is a 3 vector and u_mu is a 4 vector
    // boost is performed using Lorentz boost matrix
    // define Lambda matrix first
    double Lambda[4][4];
    double v2 = beta[0]*beta[0] + beta[1]*beta[1] + beta[2]*beta[2];
    double gamma = 1./sqrt(1. - v2);
    Lambda[0][0] = gamma;
    Lambda[0][1] = -gamma*beta[0];
    Lambda[0][2] = -gamma*beta[1];
    Lambda[0][3] = -gamma*beta[2];
    Lambda[1][1] = 1. + beta[0]*beta[0]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[1][2] = beta[0]*beta[1]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[1][3] = beta[0]*beta[2]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[2][2] = 1. + beta[1]*beta[1]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[2][3] = beta[1]*beta[2]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[3][3] = 1. + beta[2]*beta[2]*(gamma - 1.)/(v2 + 1e-15);
    for (int i = 0; i < 4; i++) {
        for (int j = i+1; j < 4; j++) {
            Lambda[j][i] = Lambda[i][j];
        }
    }

    double u_mu_new[4] = {0.0, 0.0, 0.0, 0.0};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            u_mu_new[i] += Lambda[i][j]*u_mu[j];
        }
    }
    for (int i = 0; i < 4; i++) {
        u_mu[i] = u_mu_new[i];
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
    double beta2 = beta[0]*beta[0] + beta[1]*beta[1] + beta[2]*beta[2];
    if (beta2 > 1.) {
        beta2 = 1. - 1e-12;
    }
    double gamma = 1./sqrt(1. - beta2);
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

void EM_fields::Lorentz_boost_EM_fields_tensor(double *E_lab, double *B_lab,
        double *beta, double *E_prime, double *B_prime) {
    // this function perform Lorentz boost for E_lab and B_lab fields
    // to a frame with velocity v = beta;
    // the results are stored in E_prime and B_prime vectors
    double Fmn[4][4];
    for (int i = 0; i < 4; i++) {
        Fmn[i][i] = 0.0;
    }
    Fmn[0][1] = E_lab[0];
    Fmn[0][2] = E_lab[1];
    Fmn[0][3] = E_lab[2];
    Fmn[1][2] = B_lab[2];
    Fmn[1][3] = -B_lab[1];
    Fmn[2][3] = B_lab[0];
    for (int i = 0; i < 4; i++) {
        for (int j = i+1; j < 4; j++) {
            Fmn[j][i] = -Fmn[i][j];
        }
    }
    double Lambda[4][4];
    double v2 = beta[0]*beta[0] + beta[1]*beta[1] + beta[2]*beta[2];
    double gamma = 1./sqrt(1. - v2);
    Lambda[0][0] = gamma;
    Lambda[0][1] = -gamma*beta[0];
    Lambda[0][2] = -gamma*beta[1];
    Lambda[0][3] = -gamma*beta[2];
    Lambda[1][1] = 1. + beta[0]*beta[0]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[1][2] = beta[0]*beta[1]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[1][3] = beta[0]*beta[2]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[2][2] = 1. + beta[1]*beta[1]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[2][3] = beta[1]*beta[2]*(gamma - 1.)/(v2 + 1e-15);
    Lambda[3][3] = 1. + beta[2]*beta[2]*(gamma - 1.)/(v2 + 1e-15);
    for (int i = 0; i < 4; i++) {
        for (int j = i+1; j < 4; j++) {
            Lambda[j][i] = Lambda[i][j];
        }
    }

    double Fmn_prime[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            Fmn_prime[i][j] = 0.0;
        }
    }

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            for (int k = 0; k < 4; k++) {
                for (int l = 0; l < 4; l++) {
                    Fmn_prime[i][j] += Lambda[i][k]*Lambda[j][l]*Fmn[k][l];
                }
            }
        }
    }

    E_prime[0] = Fmn_prime[0][1];
    E_prime[1] = Fmn_prime[0][2];
    E_prime[2] = Fmn_prime[0][3];
    B_prime[0] = Fmn_prime[2][3];
    B_prime[1] = -Fmn_prime[1][3];
    B_prime[2] = Fmn_prime[1][2];
}

void EM_fields::cross_product(double *a, double *b, double *c) {
    // this function calculates c = a x b
    c[0] = a[1]*b[2] - a[2]*b[1];
    c[1] = a[2]*b[0] - a[0]*b[2];
    c[2] = a[0]*b[1] - a[1]*b[0];
}
