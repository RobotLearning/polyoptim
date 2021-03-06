/*
 * Unit tests for table tennis related functionalities
 * residing in src/table_tennis.cpp
 *
 * The aims are
 *
 * 1. Check if ball functions in Table Tennis class are working well:
 *    - check if ball touches ground, hits table, etc.
 * 2. Check if the Table Tennis Player works as it should:
 *    - ball estimation
 *    - ball prediction
 *    - trajectory generation
 *
 * table_tennis.cpp
 *
 *  Created on: Feb 2, 2017
 *      Author: okoc
 */

#include <boost/test/unit_test.hpp>
#include <armadillo>
#include "player.hpp"
#include "constants.h"
#include "tabletennis.h"
#include "kinematics.hpp"
#include "kalman.h"

using namespace arma;
using namespace player;
using namespace optim;
using namespace boost::unit_test;
static void init_posture(vec7 & q0, int posture, bool verbose);

// Optim tests
void test_vhp_optim();
void test_fp_optim();
void test_dp_optim();
//void test_time_efficiency();
void find_rest_posture();
void check_accuracy_spin_based_racket_calc();
void check_speed_spin_based_racket_calc();
void test_symplectic_int_4th();

// Kinematics tests
void test_kin_deriv();
void test_kinematics_calculations();

// KF tests
void test_kf_init();
void test_kf_discretize();
void test_random_gen();
void test_predict_update();
void check_ekf();
void test_predict_path();
void check_mismatch_pred();
//void test_outlier_detection();

// Table tennis tests
void test_touch_ground();
void test_ball_ekf();
void test_player_ekf_filter();
void count_land();
void count_land_mpc();

/*
 * Main function for boost unit testing.
 *
 * Unit tests are added here.
 */
test_suite* init_unit_test_suite(int /*argc*/, char* /*argv*/[]) {

    test_suite* ts = BOOST_TEST_SUITE("test_suite");

    BOOST_TEST_MESSAGE("Testing kinematics functions...");
    ts->add(BOOST_TEST_CASE(&test_kinematics_calculations));
    ts->add(BOOST_TEST_CASE(&test_kin_deriv));

    BOOST_TEST_MESSAGE("Testing Kalman Filtering...");
    ts->add(BOOST_TEST_CASE(&test_kf_init));
    ts->add(BOOST_TEST_CASE(&test_kf_discretize));
    ts->add(BOOST_TEST_CASE(&test_random_gen));
    ts->add(BOOST_TEST_CASE(&test_predict_update));
    ts->add(BOOST_TEST_CASE(&check_ekf));
    ts->add(BOOST_TEST_CASE(&test_predict_path));
    ts->add(BOOST_TEST_CASE(&check_mismatch_pred));
    //ts->add(BOOST_TEST_CASE(&test_outlier_detection)); // TOO LONG

    BOOST_TEST_MESSAGE("Testing optimization routines...");
    ts->add(BOOST_TEST_CASE(&test_vhp_optim));
    ts->add(BOOST_TEST_CASE(&test_fp_optim));
    ts->add(BOOST_TEST_CASE(&test_dp_optim));
    ts->add(BOOST_TEST_CASE(&find_rest_posture));
    //ts->add(BOOST_TEST_CASE(&test_time_efficiency)); // TOO LONG
    ts->add(BOOST_TEST_CASE(&check_accuracy_spin_based_racket_calc));
    ts->add(BOOST_TEST_CASE(&check_speed_spin_based_racket_calc));
    ts->add(BOOST_TEST_CASE(&test_symplectic_int_4th));

    BOOST_TEST_MESSAGE("Finally testing table tennis tasks...");
    ts->add(BOOST_TEST_CASE(&test_touch_ground));
    ts->add(BOOST_TEST_CASE(&test_ball_ekf));
    ts->add(BOOST_TEST_CASE(&test_player_ekf_filter));
    ts->add(BOOST_TEST_CASE(&count_land));
    ts->add(BOOST_TEST_CASE(&count_land_mpc));

    return ts;
}

/*
 * Testing whether the ball can be returned to the opponents court
 * WITH MPC
 *
 */
void count_land_mpc() {

    BOOST_TEST_MESSAGE("Running MPC Test...");
    BOOST_TEST_MESSAGE("Counting ball landing with 3 different optim...");
    algo algs[] = {FOCUS,DP,VHP};

    for (int i = 0; i < 3; i++) {
        double Tmax = 1.0, lb[2*NDOF+1], ub[2*NDOF+1];
        set_bounds(lb,ub,0.01,Tmax);
        vec7 lbvec(lb);
        vec7 ubvec(ub);
        TableTennis tt = TableTennis(true,true);
        int num_trials = 1;
        int num_lands = 0;
        int num_misses = 0;
        int num_not_valid = 0;
        arma_rng::set_seed_random();
        //arma_rng::set_seed(0);
        double std_noise = 0.0001;
        double std_model = 0.3;
        joint qact;
        vec3 obs;
        EKF filter = init_filter(std_model,std_noise);
        player_flags flags;
        flags.alg = algs[i];
        flags.mpc = true;
        flags.freq_mpc = 1;
        flags.verbosity = 0;
        Player *robot;
        int N = 2000;
        joint qdes = qact;
        racket robot_racket;
        int ball_launch_side;
        int joint_init_pose;

        for (int n = 0; n < num_trials; n++) { // for each trial
            std::cout << "Trial: " << n+1 << std::endl;
            ball_launch_side = (randi(1,distr_param(0,2)).at(0));
            joint_init_pose = (randi(1,distr_param(0,2)).at(0));
            init_posture(qact.q,joint_init_pose,true);
            robot = new Player(qact.q,filter,flags);
            tt.reset_stats();
            tt.set_ball_gun(0.05,ball_launch_side);
            //robot.reset_filter(std_model,std_noise);
            for (int i = 0; i < N; i++) { // one trial
                obs = tt.get_ball_position() + std_noise * randn<vec>(3);
                robot->play(qact, obs, qdes);
                //robot->cheat(qact, obs, qdes);
                calc_racket_state(qdes,robot_racket);
                tt.integrate_ball_state(robot_racket,DT);
                qact.q = qdes.q;
                qact.qd = qdes.qd;
            }
            if (tt.has_legally_landed()) {
                num_lands++;
            }
            else if (!tt.has_legally_bounced())
                num_not_valid++;
            else
                num_misses++;
            delete robot;
        }

        std::cout << "======================================================" << endl;
        std::cout << "Out of " << num_trials << " trials, "
                  << num_lands << " lands, " << num_not_valid <<
                     "not valid balls, " << num_misses << " misses!" <<std::endl;
        std::cout << "======================================================" << endl;
    }
}

/*
 * Testing whether the ball can be returned to the opponents court
 * Prints also desired and actual ball (if it lands)
 */
void count_land() {

	BOOST_TEST_MESSAGE("Counting Robot Ball Landing with 3 different optim...");
    algo algs[] = {FOCUS,DP,VHP};
    for (int n = 0; n < 3; n++) {
        double Tmax = 2.0;
        double lb[2*NDOF+1], ub[2*NDOF+1];
        set_bounds(lb,ub,0.01,Tmax);
        vec7 lbvec(lb);
        vec7 ubvec(ub);
        TableTennis tt = TableTennis(false,true);
        arma_rng::set_seed_random();
        //arma_rng::set_seed(5);
        tt.set_ball_gun(0.05,0); // init ball on the centre
        double std_obs = 0.0001; // std of the noisy observations
        joint qact;
        init_posture(qact.q,1,false);
        vec3 obs;
        EKF filter = init_filter(0.03,std_obs);
        player_flags flags;
        flags.verbosity = 0;
        flags.mpc = false;
        flags.alg = algs[n];
        Player robot = Player(qact.q,filter,flags);
        int N = 2000;
        joint qdes;
        qdes.q = qact.q;
        racket robot_racket;
        mat Qdes = zeros<mat>(NDOF,N);
        for (int i = 0; i < N; i++) {
            obs = tt.get_ball_position() + std_obs * randn<vec>(3);
            robot.play(qact, obs, qdes);
            //robot.cheat(qact, tt.get_ball_state(), qdes);
            Qdes.col(i) = qdes.q;
            calc_racket_state(qdes,robot_racket);
            //cout << "robot ball dist\t" << norm(robot_racket.pos - tt.get_ball_position()) << endl;
            tt.integrate_ball_state(robot_racket,DT);
            //usleep(DT*1e6);
            qact.q = qdes.q;
            qact.qd = qdes.qd;
        }
        //cout << max(Qdes,1) << endl;

        vec2 ball_des;
        double time_des;
        robot.get_strategy(ball_des,time_des);
        BOOST_TEST_MESSAGE("Desired ball land: " << ball_des.t());
        BOOST_TEST_MESSAGE("Testing joint limits as well...");
        BOOST_TEST(all(max(Qdes,1) < ubvec));
        BOOST_TEST(all(min(Qdes,1) > lbvec));
        BOOST_TEST(tt.has_legally_landed());
        std::cout << "******************************************************" << std::endl;
    }
}

/*
 * Testing whether table tennis ball bounces on table and touches the ground
 */
void test_touch_ground() {

	BOOST_TEST_MESSAGE("Testing table tennis ball hitting ground...");
	TableTennis tt = TableTennis();

	int N = 200;
	double dt = 0.01;
	tt.set_ball_gun(0.2);
	for (int i = 0; i < N; i++) {
		tt.integrate_ball_state(dt);
	}

	vec3 ball_pos = tt.get_ball_position();
	BOOST_TEST(ball_pos(Z) == floor_level, boost::test_tools::tolerance(0.01));
}

/*
 * Testing whether the errors in the EKF filter estimate
 * are shrinking
 *
 */
void test_ball_ekf() {

	BOOST_TEST_MESSAGE("Testing EKF table tennis estimator with initial estimate error...");
	// initialize TableTennis and Filter classes
	TableTennis tt = TableTennis(false,true);
	const double std_noise = 0.001;
	const double std_model = 0.03;
	mat C = eye<mat>(3,6);
	mat66 Q = std_model * eye<mat>(6,6);
	mat33 R = std_noise * eye<mat>(3,3);
	EKF filter = EKF(calc_next_ball,C,Q,R);

	// set table tennis ball and filter
	tt.set_ball_gun(0.2);
	vec3 init_pos = tt.get_ball_position() + 0.5 * randu<vec>(3);
	vec3 init_vel = tt.get_ball_velocity() + 0.2 * randu<vec>(3);
	mat66 P0;
	P0.eye(6,6);
	P0 *= 1e6;
	vec6 x0 = join_vert(init_pos,init_vel);
	filter.set_prior(x0,P0);
	const int N = 100;
	const double dt = 0.01;
	vec3 obs;
	vec err = zeros<vec>(N);

	for (int i = 0; i < N; i++) {
		tt.integrate_ball_state(dt);
		obs = tt.get_ball_position() + std_noise * randn<vec>(3);
		filter.predict(dt);
		filter.update(obs);
		err(i) = norm(filter.get_mean() - tt.get_ball_state(),2);
	}
	//cout << err << endl;
	BOOST_TEST_MESSAGE("Error of state estimate start: " << err(0) << " end: " << err(N-1));
	BOOST_TEST(err(N-1) <= err(0), boost::test_tools::tolerance(0.0001));
}


/*
 * Testing the EKF filter of Player class
 *
 * We expect the error to be decreasing
 * at some point
 *
 */
void test_player_ekf_filter() {

	BOOST_TEST_MESSAGE("Testing Player class's Filtering performance...");

	const int N = 50;
	vec3 obs;
	vec err = zeros<vec>(N);
	const double std_noise = 0.001;
	const double std_model = 0.001;
	TableTennis tt = TableTennis(false,true);
	EKF filter = init_filter(std_model,std_noise);
	player_flags flags;
	Player cp = Player(zeros<vec>(NDOF),filter,flags);
	tt.set_ball_gun(0.2);

	for (int i = 0; i < N; i++) {
		tt.integrate_ball_state(DT);
		obs = tt.get_ball_position() + std_noise * randn<vec>(3);
		err(i) = norm(tt.get_ball_state() - cp.filt_ball_state(obs),2);
		//usleep(10e3);
	}
	//cout << err << endl;
	cout << "Error of state estimate start: " << err(0) << " end: " << err(N-1) << endl;
	BOOST_TEST(err(N-1) < err(0), boost::test_tools::tolerance(0.01));
	//cout << err;
	//BOOST_TEST(filter_est[Z] == floor_level, boost::test_tools::tolerance(0.1));
}

/*
 * Initialize robot posture
 */
static void init_posture(vec7 & q0, int posture, bool verbose) {

	rowvec qinit;
	switch (posture) {
	case 2: // right
		if (verbose)
			cout << "Initializing robot on the right side.\n";
		qinit << 1.0 << -0.2 << -0.1 << 1.8 << -1.57 << 0.1 << 0.3 << endr;
		break;
	case 1: // center
		if (verbose)
			cout << "Initializing robot on the center.\n";
		qinit << 0.0 << 0.0 << 0.0 << 1.5 << -1.75 << 0.0 << 0.0 << endr;
		break;
	case 0: // left
		if (verbose)
			cout << "Initializing robot on the left side\n";
		qinit << -1.0 << 0.0 << 0.0 << 1.5 << -1.57 << 0.1 << 0.3 << endr;
		break;
	default: // default is the right side
		qinit << 1.0 << -0.2 << -0.1 << 1.8 << -1.57 << 0.1 << 0.3 << endr;
		break;
	}
	q0 = qinit.t();
}
