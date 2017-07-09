/**
 * @file table_tennis.cpp
 *
 * @brief Table tennis file models all the table tennis interactions.
 *
 * Includes table tennis functions used for modelling table tennis interactions between
 * ball, table, racket (and robots, including possible opponents).
 *
 * Most of them are used in simulation mode, such as
 * the simulate_ball function.
 * Three methods are used to calculate desired racket quantities (which are necessary to
 * put here to use the parameters).
 *
 * We support in simulation opponent modelling, simple spin modelling, etc.
 *
 *
 */

#include <boost/program_options.hpp>
#include <armadillo>
#include "tabletennis.h"

// functions outside of Table Tennis class
static mat33 quat2mat(const vec4 & q);
static void racket_contact_model(const vec3 & racket_vel, const vec3 & racket_normal,
		                         const double & racket_param, vec3 & ball_vel);

/**
 * @brief Initialize ball pos and velocity to input vector of size 6
 * and initialize ball spin to zero.
 * @param ball_state Initial ball state (pos and vel).
 * @param spin_flag Turn ON for spin modelling.
 * @param verbosity Turn ON for printing events (bounce, hit, etc.)
 */
TableTennis::TableTennis(const vec6 & ball_state, bool spin_flag, bool verbosity)
							: SPIN_MODE(spin_flag), VERBOSE(verbosity) {
	ball_pos = ball_state(span(X,Z));
	ball_vel = ball_state(span(DX,DZ));
	ball_spin = zeros<vec>(3);
	init_topspin(params.init_topspin);
	//load_params("ball.cfg");
	//init_topspin(params.init_topspin);
}

/**
 * @brief Initialize ball variables to zero.
 *
 * Initializes all ball variables (positions,velocities,spin) to ZERO.
 *
 * @param spin_flag Turn ON for spin modelling.
 * @param verbosity Turn ON for printing events (bounce, hit, etc.)
 */
TableTennis::TableTennis(bool spin_flag, bool verbosity) :
	 SPIN_MODE(spin_flag), VERBOSE(verbosity) {

	ball_pos = zeros<vec>(3);
	ball_vel = zeros<vec>(3);
	ball_spin = zeros<vec>(3);
	init_topspin(params.init_topspin);
	//load_params("ball.cfg");
	//init_topspin(params.init_topspin);
}

/**
 * @brief Initialize constant angular velocity (a.k.a. spin)
 * for the spinning ball.
 *
 */
void TableTennis::init_topspin(const double val) {

	if (SPIN_MODE) {
		//std::cout << "Initializing with spin" << std::endl;
		ball_spin(X) = val*2*datum::pi;
		// others are zero
	}
	else {
		//std::cout << "Initializing without spin" << std::endl;
 		// do nothing, they are all zero - spinless model
	}
}

/**
 * @brief Set topspin equal to val revolutions/sec
 * @param val
 */
void TableTennis::set_topspin(const double val) {
	SPIN_MODE = true;
	ball_spin(X) = val*2*datum::pi;
}

/**
 * Reset statistics of the game.
 *
 * Statistics are stored for counting return successes in SIM mode.
 */
void TableTennis::reset_stats() {
	stats.touched_ground = false;
	stats.has_bounced = false;
	stats.legal_land = false;
	stats.hit = false;
	stats.has_landed = false;
	stats.legal_bounce = false;
}

/**
 * @brief Load ball prediction and other SIM parameters from a CONFIG file
 * @param file_name_relative Relative file name (base is polyoptim)
 *
 */
void TableTennis::load_params(const std::string & file_name_relative) {

	namespace po = boost::program_options;
	using namespace std;
	string home = std::getenv("HOME");
	string config_file = home + "/polyoptim/" + file_name_relative;

	try {
		// Declare a group of options that will be
		// allowed in config file
		po::options_description config("Configuration");
		config.add_options()
			("ball_params.CFTX", po::value<double>(&params.CFTX),
					"coefficient of table contact model on X-direction")
			("ball_params.CFTY", po::value<double>(&params.CFTY),
					"coefficient of table contact model on Y-direction")
			("ball_params.CRT", po::value<double>(&params.CRT),
					"coefficient of restitution for the table")
			("ball_params.CRR", po::value<double>(&params.CRR),
					"coefficent of restitution for racket")
		    ("ball_params.drag", po::value<double>(&params.Cdrag),
		    		"Air drag coefficient")
		    ("ball_params.gravity", po::value<double>(&params.gravity),
		    		"for simulating different gravities")
		    ("ball_params.lift", po::value<double>(&params.Clift),
		    		"coefficient of lift for the magnus force")
	        ("ball_params.init_topspin", po::value<double>(&params.init_topspin),
	    		    "initial topspin");
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
}

/**
 *
 * @brief Reset the simulated ball state.
 *
 * Set the ball-gun somewhere behind the table and launches a ball.
 * Adds noise to the initial ball launch velocity.
 * DOES NOT MODIFY SPIN (from before)!
 * Method is used for testing purposes (see Unit Tests).
 *
 * @param std Standard deviation of the initial ball pos and vel distribution.
 * @param ballgun_side Position the ballgun: 0 = LEFT, 1 = CENTER (DEFAULT), 2 = RIGHT.
 *
 */
void TableTennis::set_ball_gun(double std, int ballgun_side) {

	//using namespace std;
	vec3 ballgun = {table_center, dist_to_table - table_length - 0.2, floor_level - table_height + 0.15};
	vec3 good_ball_vel;
	switch (ballgun_side) {
	case 0:
		if (VERBOSE)
			cout << "Setting ballgun to left side..." << endl;
		good_ball_vel << -1.08 << endr << 4.80 << endr << 3.84 << endr;
		ballgun(X) += +0.4;	break;
	case 1:
		if (VERBOSE)
			cout << "Setting ballgun to center..." << endl;
		good_ball_vel << 0.0 << endr << 4.00 << endr << 3.84 << endr;
		break;
	case 2:
		if (VERBOSE)
			cout << "Setting ballgun to right side..." << endl;
		good_ball_vel << +1.08 << endr << 4.80 << endr << 3.84 << endr;
		ballgun(X) += -0.4; break;
	default:
		good_ball_vel << 0.0 << endr << 4.80 << endr << 3.84 << endr;
		// do nothing ballgun is already centred.
	}

	vec3 rand_ball_pos = ballgun + std * randn<vec>(3);
	vec3 rand_ball_vel = good_ball_vel + std * randn<vec>(3);

	this->ball_pos = rand_ball_pos;
	this->ball_vel = rand_ball_vel;
}

/**
 * @return Ball position as a 3-vector.
 */
vec3 TableTennis::get_ball_position() const {

	return this->ball_pos;
}

/**
 * @return Return ball state as a 6-vector.
 */
vec6 TableTennis::get_ball_state() const {

	return join_vert(this->ball_pos,this->ball_vel);
}

/**
 * @return Set ball state as a 6-vector.
 */
void TableTennis::set_ball_state(const vec6 & ball_state) {

	this->ball_pos = ball_state(span(X,Z));
	this->ball_vel = ball_state(span(DX,DZ));
}


/**
 * @return Ball velocity as a 3-vector.
 */
vec3 TableTennis::get_ball_velocity() const {

	return this->ball_vel;
}

/**
 *
 * @brief Main function to integrate the ball state (for dt seconds).
 *
 * This function is used to predict the ball state and typically is called
 * many times. Can be used to predict the landing point
 * (if there is a strike of course).
 *
 * Modified: February 2017
 *
 * Integrate the ball state dt seconds later.
 * Checking contacts with environment, i.e. racket, net, table, ground.
 *
 * @param robot_racket Racket of the robot for checking a strike
 * @param dt Prediction horizon.
 *
 * Takes around 1mu sec to run
 *
 */
void TableTennis::integrate_ball_state(const racket & robot_racket,
		                               const double dt) {

	// Symplectic Euler for No-Contact-Situation (Flight model)
	vec3 ball_acc = flight_model();
	vec3 ball_cand_pos, ball_cand_vel;
	symplectic_euler(dt,ball_acc, ball_cand_pos, ball_cand_vel);

	if (CHECK_CONTACTS) {
		check_contact(robot_racket,ball_cand_pos,ball_cand_vel);
	}

	// Pass the computed ball variables to ball pos and vel
	ball_pos = ball_cand_pos;
	ball_vel = ball_cand_vel;
}

/**
 *
 * @brief Integrate the ball state dt seconds later.
 * Checking contacts with environment, i.e. net, table, ground.
 * Does not check the racket!!
 * Modified: July-August 2016
 *
 * @param dt Prediction horizon.
 *
 */
void TableTennis::integrate_ball_state(const double dt) {

	// Symplectic Euler for No-Contact-Situation (Flight model)
	vec3 ball_acc = flight_model();
	vec3 ball_cand_pos, ball_cand_vel;
	symplectic_euler(dt, ball_acc, ball_cand_pos, ball_cand_vel);

	if (CHECK_CONTACTS) {
		check_ball_table_contact(ball_cand_pos,ball_cand_vel);
		check_ball_net_contact(ball_cand_pos,ball_cand_vel);
		check_ball_ground_contact(ball_cand_pos,ball_cand_vel);
	}

	// Pass the computed ball variables to ball pos and vel
	ball_pos = ball_cand_pos;
	ball_vel = ball_cand_vel;
}

/**
 * @brief Spinning nonlinear ball flight model.
 *
 * Flight model including including airdrag and Magnus force (for spin).
 *
 * @return Ball accelerations as a 3-vector.
 *
 */
vec3 TableTennis::flight_model() const {

	vec3 ball_acc = drag_flight_model();
	// add Magnus force
	if (SPIN_MODE) {
		//std::cout << "Adding some spin force" << std::endl;
		vec3 magnus = cross(ball_spin,ball_vel); // acceleration due to magnus force
		magnus *= params.Clift;
		ball_acc += magnus;
		//std::cout << magnus << std::endl;
	}
	return ball_acc;
}

/**
 * @brief Spin free nonlinear flight model including airdrag
 *
 * Cdrag is a parameter used to add drag force.
 *
 * @return Ball accelerations as 3-vector.
 */
vec3 TableTennis::drag_flight_model() const {

	double velBall = norm(ball_vel);
	vec3 ball_acc;
	ball_acc(X) = -ball_vel(X) * params.Cdrag * velBall;
	ball_acc(Y) = -ball_vel(Y) * params.Cdrag * velBall;
	ball_acc(Z) = params.gravity - ball_vel(Z) * params.Cdrag * velBall;

	return ball_acc;
}

/**
 * @brief Simple contact model that uses spin if spin mode is turned on.
 *
 * Linear contact model that updates the outgoing rebound velocities only.
 * Checks if the contact is of roll or slide type. Based on a spin model
 * from a Japanese table tennis paper.
 *
 * Note: Spin is not changed!
 * Coeff of restitution and friction used.
 *
 */
vec3 TableTennis::table_contact_model(const vec3 & ball_vel_in) const {

	static double alpha;
	vec3 ball_vel_out;

	if (SPIN_MODE) { // if spin mode is on ballvec is not a null pointer
		//cout << "Using a spin model for bounce..." << endl;
		vec3 vbT = {ball_vel_in(X) - ball_radius*ball_spin(Y),
				    ball_vel_in(Y) + ball_radius*ball_spin(X),
					0.0};
		alpha = params.mu * (1 + params.CRT) * abs(ball_vel_in(Z))/norm(vbT);
		vec3 v = {1.0-alpha,1.0-alpha,-params.CRT};
		mat Av = diagmat(v);
		mat Bv = {{0, alpha * ball_radius, 0}, {-alpha*ball_radius, 0, 0}, {0, 0, 0}};
		//cout << ball_vel;
		ball_vel_out = Av * ball_vel_in + Bv * ball_spin;
		//cout << ball_vel;
	}
	else {
		// reflect ball velocity
		//cout << "Coming here!\n" << ball_vel << endl;
		ball_vel_out(Z) = -params.CRT * ball_vel_in(Z);
		ball_vel_out(Y) = params.CFTY * ball_vel_in(Y);
		ball_vel_out(X) = params.CFTX * ball_vel_in(X);
	}
	return ball_vel_out;
}

/**
 *
 * @brief Symplectic Euler integration for dt seconds.
 *
 * First integrating the accelerations to velocities by dt
 * Then integrating the velocities to positions by dt
 * These (pos and vel) are kept in the ball candidate vector
 *
 * We dont directly update the ball states, this is done in
 * integrate_ball_state. This way we can check for contacts
 * in between.
 *
 * @param dt Prediction horizon.
 * @param ball_acc Already calculated ball accelerations
 * @param ball_next_pos Next position using calculated ball accelerations.
 * @param ball_next_vel Next velocity using calculated ball accelerations.
 */
void TableTennis::symplectic_euler(const double dt, const vec3 & ball_acc,
		vec3 & ball_next_pos, vec3 & ball_next_vel) const {

	// ball candidate velocities
	ball_next_vel(X) = ball_vel(X) + ball_acc(X) * dt;
	ball_next_vel(Y) = ball_vel(Y) + ball_acc(Y) * dt;
	ball_next_vel(Z) = ball_vel(Z) + ball_acc(Z) * dt;

	// ball candidate positions
	ball_next_pos(X) = ball_pos(X) + ball_next_vel(X) * dt;
	ball_next_pos(Y) = ball_pos(Y) + ball_next_vel(Y) * dt;
	ball_next_pos(Z) = ball_pos(Z) + ball_next_vel(Z) * dt;
}

/**
 * @brief Checks if a contact will occur.
 *
 * Checking against table, net, racket, ground, and possibly
 * a simulated human opponent!
 *
 * @param robot_racket Racket centre positions,velocities and normal of the robot
 * @param ball_cand_pos Balls next candidate positions (after symplectic int.).
 * @param ball_cand_vel Balls next candidate vels. (after symplectic int.).
 */
void TableTennis::check_contact(const racket & robot_racket,
		                        vec3 & ball_cand_pos,
		                        vec3 & ball_cand_vel) {

	// Check contact to table
	check_ball_table_contact(ball_cand_pos,ball_cand_vel);
	// Check contact with net
	check_ball_net_contact(ball_cand_pos,ball_cand_vel);
	// Check contact with racket
	check_ball_racket_contact(robot_racket,ball_cand_pos,ball_cand_vel);
	// Check if it hits the ground...
	check_ball_ground_contact(ball_cand_pos,ball_cand_vel);
}

/**
 * @brief Condition to determine if ball hits the table.
 *
 * Useful for prediction including a rebound model
 * Useful also in KF/EKF filtering.
 * If contact is detected, then a table contact model (with constant
 * parameters) will update the next candidate ball velocities.
 *
 * @param ball_cand_pos Next candidate ball positions. Used to check contact only.
 * @param ball_cand_vel Next candidate ball velocities. Updated if contact happens.
 */
void TableTennis::check_ball_table_contact(const vec3 & ball_cand_pos, vec3 & ball_cand_vel) {

	static const double contact_table_level = floor_level - table_height + ball_radius;
	static const double table_human_end = dist_to_table - table_length;

	// check to see if ball is over the table
	if ((ball_cand_pos(Y) > table_human_end) && (ball_cand_pos(Y) < dist_to_table) &&
			(fabs(ball_cand_pos(X) - table_center) <= table_width/2.0)) {
		// check if the ball hits the table coming from above
		if ((ball_cand_pos(Z) <= contact_table_level) && (ball_cand_vel(Z) < 0.0)) {
			//std::cout << "Bounce predicted!" << std::endl;
			check_legal_bounce(ball_cand_pos,ball_cand_vel);
			check_legal_land(ball_cand_pos,ball_cand_vel);
			ball_cand_vel = table_contact_model(ball_cand_vel);
		}
	}
}

/**
 * @brief Checks contact with net.
 *
 * Curious way to check contact with net:
 * if the net's distance to integrated y-state and distance to current y-state
 * signs do not match, it means that the ball is in contact with the net.
 * Then the ball candidate velocities are updated according to a (simplistic)
 * net contact model.
 *
 * @param ball_cand_pos Next candidate ball positions. Updated if contact happens.
 * @param ball_cand_vel Next candidate ball velocities. Updated if contact happens.
 */
void TableTennis::check_ball_net_contact(vec3 & ball_cand_pos, vec3 & ball_cand_vel) const {


	static const double contact_table_level = floor_level - table_height + ball_radius;
	static const double net_dist_robot = dist_to_table - 0.5 * table_length;
	static double dist_state_net;
	static double dist_cand_net;

	// Check contact with net
	if ((ball_cand_pos(Z) <= contact_table_level + net_height)
			&& (fabs(ball_cand_pos(X)) <= table_width/2.0 + net_overhang)) {
		dist_state_net = net_dist_robot - ball_pos(Y);
		dist_cand_net = net_dist_robot - ball_cand_pos(Y);

		// If on the other side of the net after integration
		// apply super simplistic model for contact with net
		if (dist_state_net >= 0.0 && dist_cand_net < 0.0) {
			if (VERBOSE)
				std::cout << "Touches the net!" << std::endl;
			// Reflect to Front
			ball_cand_vel(Y) *= -net_restitution;
			ball_cand_pos(Y) = net_dist_robot + (0.5 * net_thickness + ball_radius);
		}
		else if (dist_state_net < 0.0 && dist_cand_net >= 0.0){
			if (VERBOSE)
				std::cout << "Touches the net!" << std::endl;
			// Reflect to Back
			ball_cand_vel(Y) *= -net_restitution;
			ball_cand_pos(Y) = net_dist_robot - (0.5 * net_thickness + ball_radius);
		}
	}
}

/**
 * @brief  Checks contact with racket.
 *
 * If there is contact, then the predicted state will be transformed according to a racket-ball contact model.
 * If racket is going to hit the ball, hit data member is set to TRUE so that
 * we do not hit it the next time.
 *
 * @param robot_racket Racket center pos,vel and normals of the robot
 * @param ball_cand_pos Next candidate ball positions. Used to check contact only.
 * @param ball_cand_vel Next candidate ball velocities. Updated if contact happens.
 */
void TableTennis::check_ball_racket_contact(const racket & robot_racket,
		                                    const vec3 & ball_cand_pos,
		                                    vec3 & ball_cand_vel) {

	vec3 racket2ball = robot_racket.pos - ball_cand_pos;
	double normal_dist_ball2racket = dot(robot_racket.normal,racket2ball);
	double parallel_dist_ball2racket = sqrt(fabs(dot(racket2ball,racket2ball)
							- normal_dist_ball2racket*normal_dist_ball2racket));

	// check for contact with racket
	if ((parallel_dist_ball2racket < racket_radius) &&
			(fabs(normal_dist_ball2racket) < ball_radius) && !stats.hit) {
		stats.hit = true;
		if (VERBOSE)
			std::cout << "Contact with racket!" << std::endl;
		// Reflect to back
		racket_contact_model(robot_racket.vel, robot_racket.normal, params.CRR, ball_cand_vel);
	}
}

/**
 *
 * @brief Checks contact with ground and zeros the velocities.
 *
 * Checking contact with ground. Zeros the velocities and
 * hardsets the next candidate positions to ground level!
 *
 * @param ball_cand_pos Next candidate ball positions.
 * If contact occurs, z-position is set to floor level.
 * @param ball_cand_vel Next candidate ball velocities.
 * If contact occurs, velocities are set to zero.
 */
void TableTennis::check_ball_ground_contact(vec3 & ball_cand_pos, vec3 & ball_cand_vel) {

	if (ball_cand_pos(Z) <= floor_level) {
		if (VERBOSE && !stats.touched_ground) {// we dont want to print all the time
			std::cout << "Contact with ground Zeroing the velocities!" << std::endl;
			stats.touched_ground = true;
		}
		// zero the velocities
		ball_cand_vel = zeros<vec>(3);
		ball_cand_pos = ball_pos;
		ball_cand_pos(Z) = floor_level;
	}

}

/**
 *
 * @brief Checks for legal bounce on robot court
 * and legal landing on the opponents court
 *
 * If the ball bounced only once before being hit on the robot court
 * then it is a LEGAL_BOUNCE.
 *
 * If there was already a hit and ball hasn't bounced before,
 * then the bounce location is checked and if it is on the opponent's court
 * and if LEGAL_BOUNCE is TRUE then it is a LAND.
 */
void TableTennis::check_legal_bounce(const vec3 & ball_cand_pos, const vec3 & ball_cand_vel) {

	static const double net_y = dist_to_table - table_length/2.0;
	if (VERBOSE) {
		if (ball_cand_pos(Y) < net_y)
			std::cout << "Bounces on opponents court!" << std::endl;
		else
			std::cout << "Bounces on robot court!" << std::endl;
	}
	if (ball_cand_vel(Y) > 0) { // incoming ball
		if (ball_cand_pos(Y) > net_y && !stats.has_bounced) {
			stats.legal_bounce = true;
			stats.has_bounced = true;
		}
		else {
			stats.legal_bounce = false;
		}
	}
}

/**
 *
 * @brief Checks for legal LAND on opponents court.
 *
 * Called when ball's z-location is BELOW the table.
 * If there was already a hit and ball hasn't LANDed before
 * then if bounce location is on the opponent's court it is a LEGAL LAND.
 *
*/
void TableTennis::check_legal_land(const vec3 & ball_cand_pos, const vec3 & ball_cand_vel) {

	static const double net_y = dist_to_table - table_length/2.0;
	if (ball_cand_vel(Y) < 0 && stats.hit && !stats.has_landed) { // outgoing ball
		// checking for legal landing
		if (ball_cand_pos(Y) < net_y) { // on the human side
			stats.legal_land = true;
			if (VERBOSE) {
				std::cout << "Legal land!" << std::endl;
			}
		}
		else {
			stats.legal_land = false;
			if (VERBOSE) {
				std::cout << "Illegal land! Lost a point..." << std::endl;
			}
		}
		stats.has_landed = true;
	}
}

/**
 *
 * @brief Checks for legal landing (on the opponents court).
 *
 * Function that returns true if ball has landed legally and bounced only
 * once on OPPONENTS court.
 * Useful for generating statistics, and on robot vs. robot mode.
 *
 * @return TRUE if ball has landed legally.
 *
 */
bool TableTennis::has_legally_landed() const {

	if (stats.legal_land && stats.legal_bounce)
		return true;
	else
		return false;
}

/**
 * @brief Checks for legal bounce of the ball (on the robot court).
 * @return
 */
bool TableTennis::has_legally_bounced() const {

	return stats.legal_bounce;
}

/**
 * @brief Calculate desired racket normal assuming mirror law
 *
 * Does not use any state of the table tennis class, only the parameters.
 *
 * @param v_in Incoming ball velocity
 * @param v_out Outgoing ball velocity (desired)
 * @param normal Desired normals of the racket calculated (output)
 */
void TableTennis::calc_des_racket_normal(const mat & v_in, const mat & v_out, mat & normal) const {

	normal = v_out - v_in;
	// normalize
	normal = normalise(normal);
}


/**
 * @brief Computes the desired outgoing velocity of the ball after possible contact
 *
 * To return to a desired landing position at a desired landing time on the
 * opponents court, we calculate the desired outgoing velocities.
 * Does not use any state of the table tennis class, only the parameters.
 *
 * @param ball_land_des Desired landing position of the ball
 * @param time_land_des Time it should take for the ball to land on opponents court
 * @param balls_predicted The incoming balls that are predicted (for a fixed time horizon)
 * @param balls_out_vel Outgoing velocities on the predicted ball locations (output)
 */
void TableTennis::calc_des_ball_out_vel(const vec2 & ball_land_des,
						   const double time_land_des,
						   const mat & balls_predicted, mat & balls_out_vel) const {

	static double z_table = floor_level - table_height + ball_radius;

	// elementwise division
	balls_out_vel.row(X) = (ball_land_des(X) - balls_predicted.row(X)) / time_land_des;
	balls_out_vel.row(Y) = (ball_land_des(Y) - balls_predicted.row(Y)) / time_land_des;
	balls_out_vel.row(Z) = (z_table - balls_predicted.row(Z) -
			                0.5 * params.gravity * pow(time_land_des,2)) / time_land_des;

	//TODO: consider air drag, hack for now
	balls_out_vel.row(X) *= 1.1;
	balls_out_vel.row(Y) *= 1.1;
	balls_out_vel.row(Z) *= 1.2;
}

/**
 *
 * @brief Calculate desired racket velocity given ball incoming and outgoing velocities
 *
 * Assuming a mirror law.
 * Assumes no desired spin, i.e. racket velocity along the racket will be set to zero
 *
 * @param vel_ball_in Incoming ball velocities already predicted
 * @param vel_ball_out Outgoing desired ball velocities already calculated
 * @param racket_normal Desired racket normals already calculated
 * @param racket_vel Desired racket velocities (output)
 */
void TableTennis::calc_des_racket_vel(const mat & vel_ball_in, const mat & vel_ball_out,
		                 const mat & racket_normal, mat & racket_vel) const {

	int N = vel_ball_in.n_cols;
	for (int i = 0; i < N; i++) {
		racket_vel.col(i) = arma::dot(((vel_ball_out.col(i) + params.CRR * vel_ball_in.col(i)) /
				                        (1 + params.CRR)),
								racket_normal.col(i)) * racket_normal.col(i);
	}
}

/*
 * Forms the rotation matrix that corresponds to the quaternion
 *
 *
 */
static mat33 quat2mat(const vec4 & q) {

	mat33 R;
	R(X,X) = 2*q(X)*q(X) - 1 + 2*q(Y)*q(Y);
	R(X,Y) = 2*q(Y)*q(Z) - 2*q(X)*q(W);
	R(X,Z) = 2*q(Y)*q(W) + 2*q(X)*q(Z);
	R(Y,X) = 2*q(Y)*q(Z) + 2*q(X)*q(W);
	R(Y,Y) = 2*q(X)*q(X) - 1 + 2*q(Z)*q(Z);
	R(Y,Z) = 2*q(Z)*q(W) - 2*q(X)*q(Y);
	R(Z,X) = 2*q(Y)*q(W) - 2*q(X)*q(Z);
	R(Z,Y) = 2*q(Z)*q(W) + 2*q(X)*q(Y);
	R(Z,Z) = 2*q(X)*q(X) - 1 + 2*q(W)*q(W);
	return R;
}

/*
 * Update the incoming ball velocity with outgoing ball velocity using MIRROR LAW
 *
 * The racket contact model in vector form is O = I + (1 + eps_R)*N*N'*(V - I)
 * where I is the incoming ball velocity
 *       N is the racket normal
 *       V is the racket velocity
 *       eps_R is the coefficient of restitution of the racket
 *
 * TODO: add a spinning contact model
 *
 */
static void racket_contact_model(const vec3 & racket_vel, const vec3 & racket_normal,
		                         const double & racket_param, vec3 & ball_vel) {

	double speed = (1 + racket_param) * dot(racket_normal, racket_vel - ball_vel);
	ball_vel += speed * racket_normal;
}

/**
 * @brief Function that integrates a table tennis ball for an outside filter.
 *
 * Function exposes the table tennis integration to filters, e.g. an EKF.
 * They can use then to apply predict() using the this function pointer.
 *
 * Warning: spin is turned OFF!
 * Prediction with a spin model assumes that spin is kept constant
 * as changes to spin are not saved!
 *
 *
 * @param xnow Consists of current ball position and velocity.
 * @param dt Prediction horizon.
 * @return Next ball positions and velocities.
 */
vec calc_next_ball(const vec & xnow, const double dt, const void *fp) {

	TableTennis tennis = TableTennis(xnow,false,false);
	tennis.integrate_ball_state(dt);
	return tennis.get_ball_state();
}

/**
 * @brief Function that integrates a table tennis ball for an outside filter.
 *
 * Function exposes the table tennis integration to filters, e.g. an EKF.
 * They can use then to apply predict() using the this function pointer.
 *
 * Warning: spin is turned ON!
 *
 * @param xnow Consists of current ball position and velocity.
 * @param dt Prediction horizon.
 * @return Next ball positions and velocities.
 */
vec calc_spin_ball(const vec & xnow, const double dt, const void *fp) {

	TableTennis tennis = TableTennis(xnow,true,false);
	if (fp != nullptr) {
		double *topspin = (double*)fp;
		tennis.set_topspin(*topspin);
	}
	//tennis.set_ball_state(xnow);
	tennis.integrate_ball_state(dt);
	return tennis.get_ball_state();
}

/**
 * @brief Function that integrates a table tennis ball for an outside filter.
 * including predicting a potential racket contact.
 *
 * Overloaded function exposes the table tennis integration to filters, e.g. an EKF.
 * They can use then to apply predict() using the this function pointer.
 * This version also checks for robot's racket to predict next ball state.
 *
 * Warning: spin is turned OFF!
 *
 * @param xnow Consists of current ball position and velocity.
 * @param dt Prediction horizon.
 * @return Next ball positions and velocities.
 */
vec calc_next_ball(const racket & robot, const vec & xnow, double dt) {

	TableTennis tennis = TableTennis(xnow,false,false);
	//tennis.set_ball_state(xnow);
	tennis.integrate_ball_state(robot,dt);
	return tennis.get_ball_state();
}

/**
 * @brief Predict ball state FORWARDS till net
 *
 * Predict the ball forwards till the net so that we can look up the
 * corresponding robot joint parameters qf, qfdot, T around net.
 *
 * TODO: would it work backwards?
 * @param ball_est
 */
void predict_till_net(vec6 & ball_est) {

	const double net_y = dist_to_table - (table_length/2.0);
	TableTennis tennis = TableTennis(ball_est,false,false);
	while (ball_est(Y) < net_y) {
		tennis.integrate_ball_state(DT);
		ball_est = tennis.get_ball_state();
		//cout << ball_est << endl;
	}
}

