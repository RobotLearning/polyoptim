/**
 *
 * @file sl_interface.cpp
 *
 * @brief Interface of the Player class to the SL real-time simulator and to
 * the robot.
 *
 */

#include <boost/program_options.hpp>
#include "json.hpp"
#include <map>
#include <string>
#include <iostream>
#include <fstream>
#include <iterator>
#include <armadillo>
#include <cmath>
#include <sys/time.h>
#include "kalman.h"
#include "player.hpp"
#include "dmp.h"
#include "serve.h"
#include "tabletennis.h"
#include "sl_interface.h"
#include "ball_interface.h"

using namespace arma;
using namespace player;
using namespace serve;
using namespace const_tt;

player_flags pflags; //!< global structure for setting Player task options
serve_flags sflags; //!< global structure for setting Serve task options

/*
 *
 * Fusing the two blobs
 * If both blobs are valid last blob is preferred
 * Only updates if the blobs are valid, i.e. not obvious outliers
 *
 */
static bool fuse_blobs(const blob_state blobs[NBLOBS], vec3 & obs);

/*
 * Checks for the validity of blob ball data using obvious table tennis checks.
 * Returns TRUE if valid.
 *
 * Only checks for obvious outliers. Does not use uncertainty estimates
 * to assess validity so do not rely on it as the sole source of outlier detection!
 *
 *
 * @param blob Blob structure. Contains a status boolean
 * variable and cartesian coordinates (indices zero-to-two).
 * @param verbose If verbose is TRUE, then detecting obvious outliers will
 * print to standard output.
 * @return valid If ball is valid (status is TRUE and not an obvious outlier)
 * return true.
 */
static bool check_blob_validity(const blob_state & blob, bool verbose);

/*
 *  Set algorithm to initialize Player with.
 *  alg_num selects between three algorithms: VHP/FOCUSED/DEFENSIVE.
 */
static void set_player_algorithm(const int alg_num);

void set_player_algorithm(const int alg_num) {

	switch (alg_num) {
		case 0:
			std::cout << "Setting to FOCUSED player..." << std::endl;
			pflags.alg = FOCUS;
			break;
		case 1:
			std::cout << "Setting to DEFENSIVE player..." << std::endl;
			pflags.alg = DP;
			break;
		case 2:
			std::cout << "Setting to VHP player..." << std::endl;
			pflags.alg = VHP;
			break;
		default:
			pflags.alg = FOCUS;
	}
}

void load_player_options() {

	namespace po = boost::program_options;
	using namespace std;

	pflags.reset = true;
	string home = std::getenv("HOME");
	string config_file = home + "/table-tennis/config/" + "player.cfg";
	int alg_num;

    try {
		// Declare a group of options that will be
		// allowed in config file
		po::options_description config("Configuration");
		config.add_options()
		    ("outlier_detection", po::value<bool>(&pflags.outlier_detection)->default_value(true),
			      "OUTLIER DETECTION FOR REAL ROBOT!")
		    ("rejection_multiplier", po::value<double>(&pflags.out_reject_mult)->default_value(2.0),
				"OUTLIER DETECTION MULTIPLIER FOR REAL ROBOT!")
			("check_bounce", po::value<bool>(&pflags.check_bounce),
			      "checking bounce before moving!")
		    ("weights", po::value<std::vector<double>>(&pflags.weights)->multitoken(), "hit,net,land weights for DP")
			("mult_vel", po::value<std::vector<double>>(&pflags.mult_vel)->multitoken(), "velocity mult. for DP")
			("penalty_loc", po::value<std::vector<double>>(&pflags.penalty_loc)->multitoken(), "punishment locations for DP")
			("rest_posture_optim", po::value<bool>(&pflags.optim_rest_posture)->default_value(false),
				"turn on resting state optimization")
			//("lookup", po::value<bool>(&flags.lookup), "start moving with lookup")
			("algorithm", po::value<int>(&alg_num)->default_value(0),
				  "optimization method")
			("mpc", po::value<bool>(&pflags.mpc)->default_value(false),
				 "corrections (MPC)")
			("spin", po::value<bool>(&pflags.spin)->default_value(false),
						 "apply spin model")
			("verbose", po::value<int>(&pflags.verbosity)->default_value(1),
		         "verbosity level")
		    ("save_data", po::value<bool>(&pflags.save)->default_value(false),
		         "saving robot/ball data")
		    ("ball_land_des_x_offset", po::value<double>(&pflags.ball_land_des_offset[0]),
		    	 "ball land x offset")
			("ball_land_des_y_offset", po::value<double>(&pflags.ball_land_des_offset[1]),
				 "ball land y offset")
		    ("time_land_des", po::value<double>(&pflags.time_land_des),
		    	 "time land des")
			("start_optim_offset", po::value<double>(&pflags.optim_offset),
				 "start optim offset")
			("time2return", po::value<double>(&pflags.time2return),
						 "time to return to start posture")
			("freq_mpc", po::value<int>(&pflags.freq_mpc), "frequency of updates")
		    ("min_obs", po::value<int>(&pflags.min_obs), "minimum obs to start filter")
		    ("var_noise", po::value<double>(&pflags.var_noise), "std of filter obs noise")
		    ("var_model", po::value<double>(&pflags.var_model), "std of filter process noise")
		    ("t_reset_threshold", po::value<double>(&pflags.t_reset_thresh), "filter reset threshold time")
		    ("VHPY", po::value<double>(&pflags.VHPY), "location of VHP")
		    ("url", po::value<std::string>(&pflags.zmq_url), "TCP URL for ZMQ connection")
		    ("debug_vision", po::value<bool>(&pflags.debug_vision), "print ball in listener");
        po::variables_map vm;
        ifstream ifs(config_file.c_str());
        if (!ifs) {
            cout << "can not open config file: " << config_file << "\n";
        }
        else {
            po::store(parse_config_file(ifs, config), vm);
            notify(vm);
        }
    }
    catch(exception& e) {
        cout << e.what() << "\n";
    }
    set_player_algorithm(alg_num);
    pflags.detach = true; // always detached in SL/REAL ROBOT!
}



void play_new(const SL_Jstate joint_state[NDOF+1],
          SL_DJstate joint_des_state[NDOF+1]) {

    // connect to ZMQ server
    // acquire ball info from ZMQ server
    // if new ball add status true else false
    // call play function
    static Listener listener(pflags.zmq_url,pflags.debug_vision);

    // since old code support multiple blobs
    static blob_state blobs[NBLOBS];

    // update ball info
    listener.fetch(blobs[1]);

    // interface that supports old setup
    play(joint_state,blobs,joint_des_state);
}


void play(const SL_Jstate joint_state[NDOF+1],
		  const blob_state blobs[NBLOBS],
		  SL_DJstate joint_des_state[NDOF+1]) {

	static vec7 q0;
	static vec3 ball_obs;
	static optim::joint qact;
	static optim::joint qdes;
	static Player *robot = nullptr;
	static std::string home = std::getenv("HOME");
	static std::string ball_file = home + "/table-tennis/balls.txt";
	static EKF filter = init_ball_filter(0.3,0.001,pflags.spin);

	if (pflags.reset) {
		for (int i = 0; i < NDOF; i++) {
			qdes.q(i) = q0(i) = joint_state[i+1].th;
			qdes.qd(i) = 0.0;
			qdes.qdd(i) = 0.0;
		}
		filter = init_ball_filter(0.3,0.001,pflags.spin);
		delete robot;
		robot = new Player(q0,filter,pflags);
		pflags.reset = false;
	}
	else {
		for (int i = 0; i < NDOF; i++) {
			qact.q(i) = joint_state[i+1].th;
			qact.qd(i) = joint_state[i+1].thd;
			qact.qdd(i) = joint_state[i+1].thdd;
		}
		fuse_blobs(blobs,ball_obs);
		robot->play(qact,ball_obs,qdes);
		if (pflags.save)
		    save_ball_data(blobs,filter);
	}

	// update desired joint state
	for (int i = 0; i < NDOF; i++) {
		joint_des_state[i+1].th = qdes.q(i);
		joint_des_state[i+1].thd = qdes.qd(i);
		joint_des_state[i+1].thdd = qdes.qdd(i);
	}
}

void cheat(const SL_Jstate joint_state[NDOF+1],
          const SL_Cstate sim_ball_state,
          SL_DJstate joint_des_state[NDOF+1]) {

    static vec7 q0;
    static vec6 ball_state;
    static optim::joint qact;
    static optim::joint qdes;
    static Player *cp; // centered player
    static EKF filter = init_ball_filter();

    if (pflags.reset) {
        for (int i = 0; i < NDOF; i++) {
            qdes.q(i) = q0(i) = joint_state[i+1].th;
            qdes.qd(i) = 0.0;
            qdes.qdd(i) = 0.0;
        }
        cp = new Player(q0,filter,pflags);
        pflags.reset = false;
    }
    else {
        for (int i = 0; i < NDOF; i++) {
            qact.q(i) = joint_state[i+1].th;
            qact.qd(i) = joint_state[i+1].thd;
            qact.qdd(i) = joint_state[i+1].thdd;
        }
        for (int i = 0; i < NCART; i++) {
            ball_state(i) = sim_ball_state.x[i+1];
            ball_state(i+NCART) = sim_ball_state.xd[i+1];
        }
        cp->cheat(qact,ball_state,qdes);
    }

    // update desired joint state
    for (int i = 0; i < NDOF; i++) {
        joint_des_state[i+1].th = qdes.q(i);
        joint_des_state[i+1].thd = qdes.qd(i);
        joint_des_state[i+1].thdd = qdes.qdd(i);
    }
}

void save_joint_data(const SL_Jstate joint_state[NDOF+1],
                     const SL_DJstate joint_des_state[NDOF+1],
                     const int save_qdes) {

    static rowvec q = zeros<rowvec>(NDOF);
    static rowvec qdes = zeros<rowvec>(NDOF);
    static bool firsttime = true;
    static std::ofstream stream_joints;
    static std::string home = std::getenv("HOME");
    static std::string joint_file = home + "/table-tennis/joints.txt";
    if (firsttime) {
        stream_joints.open(joint_file,std::ofstream::out);
        firsttime = false;
    }

    for (int i = 1; i <= NDOF; i++) {
        q(i-1) = joint_state[i].th;
        if (save_qdes) {
            qdes(i-1) = joint_des_state[i].th;
        }
    }

    if (stream_joints.is_open()) {
        if (save_qdes) {
            stream_joints << join_horiz(q,qdes);
        }
        else {
            stream_joints << q;
        }
    }
    //stream_joints.close();
}

void save_ball_data(char* url_string, int debug_vision) {

    static Listener listener(url_string,(bool)debug_vision);
    static blob_state blobs[NBLOBS];

    // update blobs structure with new ball data
    listener.fetch(blobs[1]);

    static bool firsttime = true;
    static std::ofstream stream_balls;
    static std::string home = std::getenv("HOME");
    static std::string ball_file = home + "/table-tennis/balls.txt";

    if (firsttime) {
        stream_balls.open(ball_file,std::ofstream::out);
        firsttime = false;
    }
    vec6 ball_vec;

    if (blobs[0].status || blobs[1].status) {

        try {
            ball_vec << 0 << blobs[0].status << blobs[0].pos[X] << blobs[0].pos[Y] << blobs[0].pos[Z]
                      << 1 << blobs[1].status << blobs[1].pos[X] << blobs[1].pos[Y] << blobs[1].pos[Z] << endr;

            if (stream_balls.is_open()) {
                stream_balls << ball_vec;
            }
        }
        catch (const std::exception & not_init_error) {
            // filter not yet initialized
        }
        //stream_balls.close();
    }
}

void save_ball_data(const blob_state blobs[NBLOBS],
                    const KF & filter) {

    static rowvec ball_full;
    static bool firsttime = true;
    static std::ofstream stream_balls;
    static std::string home = std::getenv("HOME");
    static std::string ball_file = home + "/table-tennis/balls.txt";
    if (firsttime) {
        stream_balls.open(ball_file,std::ofstream::out);
        firsttime = false;
    }
    vec6 ball_est = zeros<vec>(6);

    if (blobs[0].status || blobs[1].status) {

        try {
            ball_est = filter.get_mean();
            ball_full << 0 << blobs[0].status << blobs[0].pos[X] << blobs[0].pos[Y] << blobs[0].pos[Z]
                      << 1 << blobs[1].status << blobs[1].pos[X] << blobs[1].pos[Y] << blobs[1].pos[Z] << endr;
            //cout << ball_full << endl;
            ball_full = join_horiz(ball_full,ball_est.t());
            if (stream_balls.is_open()) {
                stream_balls << ball_full;
            }
        }
        catch (const std::exception & not_init_error) {
            // filter not yet initialized
        }
        //stream_balls.close();
    }
}

void load_serve_options(double custom_pose[], serve_task_options *options) {

    namespace po = boost::program_options;
    using std::string;
    const std::string home = std::getenv("HOME");
    string config_file = home + "/table-tennis/config/serve.cfg";

    try {
        // Declare a group of options that will be
        // allowed in config file
        po::options_description config("Configuration");
        config.add_options()
        ("json_file", po::value<string>(&sflags.json_file),
        "JSON File to load DMP values from")
        ("start_dmp_from_act_state", po::value<bool>(&sflags.start_dmp_from_act_state)->default_value(false),
        "Evolve DMP from actual sensor state vs. desired state")
        ("save_act_joint", po::value<bool>(&sflags.save_joint_act_data),
        "Save act joint data during movement to txt file")
        ("save_des_joint", po::value<bool>(&sflags.save_joint_des_data),
        "Save des joint data during movement to txt file")
        ("save_ball_data", po::value<bool>(&sflags.save_ball_data),
        "Save ball data acquired via Listener to txt file")
        ("use_inv_dyn_fb", po::value<bool>(&sflags.use_inv_dyn_fb),
        "Use computed-torque control if false")
        ("url", po::value<std::string>(&pflags.zmq_url), "TCP URL for ZMQ connection")
        ("debug_vision", po::value<bool>(&pflags.debug_vision), "print ball in listener")
        ("detach", po::value<bool>(&sflags.detach)->default_value(true),
              "detach optimization if true")
        ("mpc", po::value<bool>(&sflags.mpc)->default_value(false),
             "run optimization to correct for mispredictions etc.")
        ("freq_mpc", po::value<int>(&sflags.freq_mpc)->default_value(1),
             "frequency of running optimization for corrections")
        ("verbose", po::value<bool>(&sflags.verbose)->default_value(false),
             "verbosity level")
        ("ball_land_des_x_offset", po::value<double>(&pflags.ball_land_des_offset[0]),
             "ball land x offset")
        ("ball_land_des_y_offset", po::value<double>(&pflags.ball_land_des_offset[1]),
             "ball land y offset")
        ("time_land_des", po::value<double>(&pflags.time_land_des),
             "time land des");
        po::variables_map vm;
        std::ifstream ifs(config_file.c_str());
        if (!ifs) {
            cout << "can not open config file: " << config_file << "\n";
        }
        else {
            po::store(parse_config_file(ifs, config), vm);
            notify(vm);
        }
    }
    catch(std::exception& e) {
        cout << e.what() << "\n";
    }
    options->use_inv_dyn_fb = sflags.use_inv_dyn_fb;
    using dmps = Joint_DMPs;
    string json_file = home + "/table-tennis/json/" + sflags.json_file;
    dmps multi_dmp = dmps(json_file);
    vec7 pose;
    multi_dmp.get_init_pos(pose);
    for (int i = 0; i < NDOF; i++) {
        custom_pose[i] = pose(i);
    }
    sflags.reset = true;

}

void serve_ball(const SL_Jstate joint_state[],
                 SL_DJstate joint_des_state[],
                 serve_task_options * opt) {

    static vec3 ball_obs;
    static Listener listener(pflags.zmq_url,pflags.debug_vision);
    static blob_state blobs[NBLOBS];
    using dmps = Joint_DMPs;
    static dmps multi_dmp;
    static optim::joint qact;
    static optim::joint qdes;
    static ServeBall *robot = nullptr; // class for serving ball
    static EKF filter = init_ball_filter(0.3,0.001,false);

    // update blobs structure with new ball data
    listener.fetch(blobs[1]);

    if (sflags.reset) {
        std::string home = std::getenv("HOME");
        std::string file = home + "/table-tennis/json/" + sflags.json_file;
        multi_dmp = dmps(file);
        if (sflags.start_dmp_from_act_state) {
            vec7 qinit;
            for (int i = 0; i < NDOF; i++) {
                qinit(i) = joint_state[i+1].th;
            }
            multi_dmp.set_init_pos(qinit);
        }
        for (int i = 0; i < NDOF; i++) {
            qdes.q(i) = joint_state[i+1].th;
            qdes.qd(i) = 0.0;
            qdes.qdd(i) = 0.0;
        }
        filter = init_ball_filter(0.3,0.001,false);
        delete robot;
        robot = new ServeBall(multi_dmp);
        robot->set_flags(sflags);
        sflags.reset = false;
    }
    else {
        for (int i = 0; i < NDOF; i++) {
            qact.q(i) = joint_state[i+1].th;
            qact.qd(i) = joint_state[i+1].thd;
            qact.qdd(i) = joint_state[i+1].thdd;
        }
        fuse_blobs(blobs,ball_obs);
        estimate_ball_state(ball_obs,filter);
        robot->serve(filter,qact,qdes);
        if (sflags.save_joint_act_data)
            save_joint_data(joint_state,joint_des_state,sflags.save_joint_des_data);
        if (sflags.save_ball_data)
            save_ball_data(blobs,filter);
    }

    // update desired joint state
    for (int i = 0; i < NDOF; i++) {
        joint_des_state[i+1].th = qdes.q(i);
        joint_des_state[i+1].thd = qdes.qd(i);
        joint_des_state[i+1].thdd = qdes.qdd(i);
    }
}

static bool check_blob_validity(const blob_state & blob, bool verbose) {

    bool valid = true;
    static double last_blob[NCART];
    static double zMax = 0.5;
    static double zMin = floor_level - table_height;
    static double xMax = table_width/2.0;
    static double yMax = 0.5;
    static double yMin = dist_to_table - table_length - 1.0;
    static double yCenter = dist_to_table - table_length/2.0;

    // if both blobs are valid
    // then check for both
    // else check only one

    if (blob.status == false) {
        valid = false;
    }
    else if (blob.pos[Z] > zMax) {
        if (verbose)
            printf("BLOB NOT VALID! Ball is above zMax = 0.5!\n");
        valid = false;
    }
    // on the table blob should not appear under the table
    else if (fabs(blob.pos[X]) < xMax && fabs(blob.pos[Y] - yCenter) < table_length/2.0
            && blob.pos[Z] < zMin) {
        if (verbose)
            printf("BLOB NOT VALID! Ball appears under the table!\n");
        valid = false;
    }
    else if (last_blob[Y] < yCenter && blob.pos[Y] > dist_to_table) {
        if (verbose)
            printf("BLOB NOT VALID! Ball suddenly jumped in Y!\n");
        valid = false;
    }
    else if (blob.pos[Y] < yMin || blob.pos[Y] > yMax) {
        if (verbose)
            printf("BLOB NOT VALID! Ball is outside y limits [%f,%f]!\n", yMin, yMax);
        valid = false;
    }

    last_blob[X] = blob.pos[X];
    last_blob[Y] = blob.pos[Y];
    last_blob[Z] = blob.pos[Z];
    return valid;
}

static bool fuse_blobs(const blob_state blobs[NBLOBS], vec3 & obs) {

    bool status = false;

    // if ball is detected reliably
    // Here we hope to avoid outliers and prefer the blob3 over blob1
    if (check_blob_validity(blobs[1],pflags.verbosity > 2) ||
            check_blob_validity(blobs[0],pflags.verbosity > 2)) {
        status = true;
        if (blobs[1].status) { //cameras 3 and 4
            for (int i = X; i <= Z; i++)
                obs(i) = blobs[1].pos[i];
        }
        else { // cameras 1 and 2
            for (int i = X; i <= Z; i++)
                obs(i) = blobs[0].pos[i];
        }
    }
    return status;
}
