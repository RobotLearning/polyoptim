/*
 * serve.cpp
 *
 *  Created on: Jul 22, 2018
 *      Author: okoc
 */
#include "dmp.h"
#include <armadillo>
#include <algorithm>
#include <string>
#include "player.hpp"
#include "tabletennis.h"
#include "kinematics.hpp"
#include "kalman.h"
#include "serve.h"

using namespace arma;
using namespace const_tt;
using namespace player;
using namespace optim;

namespace serve {

using dmps = Joint_DMPs;
using vec_str = std::vector<std::string>;

static bool predict_ball_hit(const player::EKF & filter,
                      const vec7 & q_rest_des,
                      const dmps & multi_dmp,
                      const optim::spline_params & p,
                      const double & t_poly,
                      const int N,
                      const int i,
                      const bool & ran_optim);

dmps init_dmps() {
    const std::string home = std::getenv("HOME");
    vec_str files = get_files(home + "/table-tennis/json/");
    arma_rng::set_seed_random();
    int val = (randi(1,distr_param(0,files.size()-1)).at(0));
    std::string file = files[val];
    std::cout << "Loading DMP " << file << std::endl;
    std::string full_file = home + "/table-tennis/json/" + file;
    return dmps(full_file);
}

ServeBall::ServeBall(dmps & multi_dmp_) : multi_dmp(multi_dmp_) {
    T = 1.0;
    double Tmax = 2.0;
    double lb[2*NDOF+1], ub[2*NDOF+1];
    set_bounds(lb,ub,0.01,Tmax);
    multi_dmp.get_init_pos(q_rest_des);
    opt = new FocusedOptim(q_rest_des,lb,ub);

}

ServeBall::~ServeBall() {
    delete opt;
}

void ServeBall::set_flags(const serve_flags & sflags_) {
    sflags = sflags_;
}

void ServeBall::serve(const EKF & filter,
                      const joint & qact,
                      joint & qdes) {

    const int N = T/DT;
    static spline_params p;
    static int i = 0;
    static double t_poly = 0.0;
    static wall_clock timer;
    static bool firsttime = true;

    if (firsttime) {
        timer.tic();
        firsttime = false;
    }

    if (opt->get_params(qact,p)) {
        t_poly = 0.0;
        ran_optim = true;
        opt->set_moving(true);
        update_next_state(p,q_rest_des,1.0,t_poly,qdes);
    }
    else {
        multi_dmp.step(DT,qdes);
    }

    if (sflags.mpc && (timer.toc() > (1.0/sflags.freq_mpc)) &&
            !predict_ball_hit(filter,q_rest_des,multi_dmp,p,t_poly,N,i,ran_optim)) {
        // launch optimizer
        cout << "Ball won't be hit! Launching optimization to correct traj...\n";
        correct_with_optim(qact,filter);
        timer.tic();
    }
    else {
        //cout << "Predicting ball hit..." << endl;
    }
    i++;
}

void ServeBall::correct_with_optim(const joint & qact,
                                    const EKF & filter) {

    double Tpred = 2.0;
    mat balls_pred = zeros<mat>(6,Tpred/DT);
    predict_ball(Tpred,balls_pred,filter);

    //lookup_soln(filter.get_mean(),1,qact);
    double time_land_des = sflags.time_land_des;
    vec2 ball_land_des;
    ball_land_des(X) = sflags.ball_land_des_x_offset;
    ball_land_des(Y) = sflags.ball_land_des_y_offset + dist_to_table - 1*table_length/4;
    optim_des pred_params;
    pred_params.Nmax = 1000;
    calc_racket_strategy(balls_pred,ball_land_des,time_land_des,pred_params);
    FocusedOptim *fp = static_cast<FocusedOptim*>(opt);
    fp->set_return_time(1.0);
    fp->set_detach(sflags.detach);
    fp->set_des_params(&pred_params);
    fp->set_verbose(sflags.verbose);
    fp->update_init_state(qact);
    fp->run();
}

void estimate_ball_state(const vec3 & obs, EKF & filter) {

    const int min_ball_to_start_filter = 5;
    static mat init_obs = zeros<mat>(3,min_ball_to_start_filter);
    static vec init_times = zeros<vec>(min_ball_to_start_filter);
    static int idx = 0;

    // FILTERING
    if (idx == min_ball_to_start_filter) {
        vec6 init_filter_state;
        estimate_ball_linear(init_obs,init_times,false,init_filter_state);
        mat P;
        P.eye(6,6);
        filter.set_prior(init_filter_state,P);
    }
    else if (idx < min_ball_to_start_filter) {
        init_obs.col(idx) = obs;
        init_times(idx) = idx*DT;
    }
    else {
        filter.predict(DT,true);
        filter.update(obs);
    }
}

/*
 * If the filter was initialized then we have enough info
 * to predict what will happen to the ball.
 *
 * If a hit is predicted then returns true.
 */
static bool predict_ball_hit(const EKF & filter,
                      const vec7 & q_rest_des,
                      const dmps & multi_dmp,
                      const spline_params & p,
                      const double & t_poly,
                      const int N,
                      const int i,
                      const bool & ran_optim) {

    // PREDICT IF BALL WILL BE HIT, OTHERWISE LAUNCH TRAJ OPTIM

    try {
        TableTennis tt_pred = TableTennis(filter.get_mean(),false,false);
        racket robot_pred_racket;
        optim::joint Q_pred;
        unsigned N_pred;

        if (!ran_optim) {
            dmps multi_pred_dmp = multi_dmp;
            N_pred = N-i;
            for (unsigned j = 0; j < N_pred; j++) {
                multi_pred_dmp.step(DT,Q_pred);
                calc_racket_state(Q_pred,robot_pred_racket);
                tt_pred.integrate_ball_state(robot_pred_racket,DT);
            }
        }
        else {
            double t_poly_pred = t_poly;
            N_pred = (p.time2hit - t_poly)/DT;
            for (unsigned j = 0; j < N_pred; j++) {
                update_next_state(p,q_rest_des,1.0,t_poly_pred,Q_pred);
                calc_racket_state(Q_pred,robot_pred_racket);
                tt_pred.integrate_ball_state(robot_pred_racket,DT);
            }
        }
        return tt_pred.was_served();
    }
    catch (const std::exception & not_init_error) {
        // filter not yet initialized
        return true;
    }
}

vec_str get_files(std::string folder_name) {

    using std::string;
    vec_str files;
    FILE * stream;
    const int max_buffer = 256;
    char buffer[max_buffer];
    string cmd = "ls " + folder_name;
    //cmd.append(" 2>&1");

    stream = popen(cmd.c_str(), "r");
    if (stream) {
        while (!feof(stream)) {
            if (fgets(buffer, max_buffer, stream) != NULL) {
                files.push_back(buffer);
            }
        }
        pclose(stream);
    }
    // remove newline from each file
    for (std::string & file : files) {
        file.erase(std::remove(file.begin(),file.end(),'\n'),file.end());
    }
    return files;
}

}