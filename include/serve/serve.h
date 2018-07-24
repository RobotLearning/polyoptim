/*
 * serve.h
 *
 *  Created on: Jul 22, 2018
 *      Author: okoc
 */

#ifndef INCLUDE_SERVE_H_
#define INCLUDE_SERVE_H_

namespace serve {

using dmps = Joint_DMPs;
using vec_str = std::vector<std::string>;

/**
 * \brief Flags used in the SERVE class and also in the SERVE task in SL.
 */
struct serve_flags {
    bool detach = false; //!< detach optimization
    bool mpc = true; //!< run optimization if DMP is predicted to miss target
    bool verbose = false; //!< print optim info if true
    bool reset = false; //!< reset the serve class
    bool save_joint_act_data = false;
    bool save_joint_des_data = false;
    bool save_ball_data = false;
    bool start_dmp_from_act_state = false;
    bool use_inv_dyn_fb = false; //!< in SL apply inv. dyn. feedback
    std::string json_file = "dmp4.json"; //!< json file to load dmp from
    std::string zmq_url = "tcp://helbe:7660"; //!< URL for ZMQ connection
    bool debug_vision = false; //!< print received vision info
    int freq_mpc = 1.0; //!< how many times per minute to re-run optim
    double time_land_des = 0.6; //!< desired time to land on robot court first
    double ball_land_des_x_offset = 0.0;
    double ball_land_des_y_offset = 0.0;
};

class ServeBall {

private:
    bool ran_optim = false;
    serve_flags sflags;
    dmps multi_dmp;
    double T = 1.0;
    vec7 q_rest_des;
    vec7 q_hit_des;
    optim::Optim *opt = nullptr; // optimizer

public:

    /**
     * \brief Serve a table tennis ball
     *
     * Start by following a DMP and then repeatedly correct
     * with optimization whenever the predicted ball is not hit by the
     * predicted movement.
     */
    void serve(const player::EKF & filter,
               const joint & qact,
               joint & qdes);

    /**\brief Initialize serve class with a DMP and setup optimization for corrections later. */
    ServeBall(dmps & multi_dmp_);
    ~ServeBall();

    void set_flags(const serve_flags & sflags_);

    /*
     * \brief Correct movement by running an optimization.
     * The plan is to serve the ball to a desired position on robot court.
     */
    void correct_with_optim(const joint & qact,
                            const player::EKF & filter);
};

/** \brief Initialize DMP from a random JSON file */
dmps init_dmps();

/** \brief Estimate ball state using a few initial ball estimates.*/
void estimate_ball_state(const vec3 & obs, player::EKF & filter);

/** \brief Utility function to return vector of JSON files from subfolder */
vec_str get_files(std::string folder_name);

}

#endif /* INCLUDE_SERVE_SERVE_H_ */