// cart_path_planner_lib: 
// wsn, June, 2015
// a library of arm-motion planning functions
#include <cartesian_moves/cart_path_planner_lib.h>

//constructor:
CartTrajPlanner::CartTrajPlanner() // optionally w/ args, e.g. 
//: as_(nh_, "CartTrajPlanner", boost::bind(&cartTrajActionServer::executeCB, this, _1), false)
{
    ROS_INFO("in constructor of CartTrajPlanner...");
   //define a fixed orientation: tool flange pointing down, with x-axis forward
    b_des_ << 0, 0, -1;
    n_des_ << 1, 0, 0;
    t_des_ = b_des_.cross(n_des_);
    
    R_gripper_down_.col(0) = n_des_;
    R_gripper_down_.col(1) = t_des_;
    R_gripper_down_.col(2) = b_des_;
}

//specify start and end poses w/rt torso.  Only orientation of end pose will be considered; orientation of start pose is ignored
bool CartTrajPlanner::cartesian_path_planner(Eigen::Affine3d a_tool_start,Eigen::Affine3d a_tool_end, std::vector<Eigen::VectorXd> &optimal_path) {
    std::vector<std::vector<Eigen::VectorXd> > path_options;
    path_options.clear();
    std::vector<Eigen::VectorXd> single_layer_nodes;
    Eigen::VectorXd node;
    Eigen::Affine3d a_tool_des;
    Eigen::Matrix3d R_des = a_tool_end.linear();
    a_tool_des.linear() = R_des;

    int nsolns;
    bool reachable_proposition;
    int nsteps = 0;
    Eigen::Vector3d p_des,dp_vec,del_p,p_start,p_end;
    p_start = a_tool_start.translation();
    p_end = a_tool_end.translation();
    del_p = p_end-p_start;
    double dp_scaler = 0.05;
    nsteps = round(del_p.norm()/dp_scaler);
    if (nsteps<1) nsteps=1;
    dp_vec = del_p/nsteps;
    nsteps++; //account for pose at step 0


    std::vector<Vectorq7x1> q_solns;
    p_des = p_start;

    for (int istep=0;istep<nsteps;istep++) 
    {
            a_tool_des.translation() = p_des;
            cout<<"trying: "<<p_des.transpose()<<endl;
            nsolns = baxter_IK_solver_.ik_solve_approx_wrt_torso(a_tool_des, q_solns);
            std::cout<<"nsolns = "<<nsolns<<endl;
            single_layer_nodes.clear();
            if (nsolns>0) {
                single_layer_nodes.resize(nsolns);
                for (int isoln = 0; isoln < nsolns; isoln++) {
                    // this is annoying: can't treat std::vector<Vectorq7x1> same as std::vector<Eigen::VectorXd> 
                    node = q_solns[isoln];
                    single_layer_nodes[isoln] = node;
                }

                path_options.push_back(single_layer_nodes);
            }
            p_des += dp_vec;
    }

    //plan a path through the options:
    int nlayers = path_options.size();
    if (nlayers < 1) {
        ROS_WARN("no viable options: quitting");
        return false; // give up if no options
    }
    //std::vector<Eigen::VectorXd> optimal_path;
    optimal_path.resize(nlayers);
    double trip_cost;
    // set joint penalty weights for computing optimal path in joint space
    Eigen::VectorXd weights;
    weights.resize(VECTOR_DIM);
    for (int i = 0; i < VECTOR_DIM; i++) {
        weights(i) = 1.0;
    }

    // compute min-cost path, using Stagecoach algorithm
    cout << "instantiating a JointSpacePlanner:" << endl;
    { //limit the scope of jsp here:
        JointSpacePlanner jsp(path_options, weights);
        cout << "recovering the solution..." << endl;
        jsp.get_soln(optimal_path);
        trip_cost = jsp.get_trip_cost();
    }

    //now, jsp is deleted, but optimal_path lives on:
    cout << "resulting solution path: " << endl;
    for (int ilayer = 0; ilayer < nlayers; ilayer++) {
        cout << "ilayer: " << ilayer << " node: " << optimal_path[ilayer].transpose() << endl;
    }
    cout << "soln min cost: " << trip_cost << endl;
    return true;
}


// alt version: specify start as a q_vec, and goal as a Cartesian pose (w/rt torso)
bool CartTrajPlanner::cartesian_path_planner(Vectorq7x1 q_start,Eigen::Affine3d a_tool_end, std::vector<Eigen::VectorXd> &optimal_path) {
    std::vector<std::vector<Eigen::VectorXd> > path_options;
    path_options.clear();
    std::vector<Eigen::VectorXd> single_layer_nodes;
    Eigen::VectorXd node;
    Eigen::Affine3d a_tool_des,a_tool_start;
    Eigen::Matrix3d R_des = a_tool_end.linear();
    a_tool_start = baxter_fwd_solver_.fwd_kin_solve_wrt_torso(q_start);
    cout<<"fwd kin from q_start: "<<a_tool_start.translation().transpose()<<endl;
    cout<<"fwd kin from q_start R: "<<endl;
    cout<<a_tool_start.linear()<<endl;
    
    a_tool_start.linear() = R_des; // override the orientation component--require point down    
    a_tool_des.linear() = R_des;

    int nsolns;
    bool reachable_proposition;
    int nsteps = 0;
    Eigen::Vector3d p_des,dp_vec,del_p,p_start,p_end;
    p_start = a_tool_start.translation();
    p_end = a_tool_end.translation();
    del_p = p_end-p_start;
    cout<<"p_start: "<<p_start.transpose()<<endl;
    cout<<"p_end: "<<p_end.transpose()<<endl;
    cout<<"del_p: "<<del_p.transpose()<<endl;
    double dp_scaler = 0.05;
    nsteps = round(del_p.norm()/dp_scaler);
    if (nsteps<1) nsteps=1;
    dp_vec = del_p/nsteps;
    cout<<"dp_vec for nsteps = "<<nsteps<<" is: "<<dp_vec.transpose()<<endl;
    nsteps++; //account for pose at step 0

    //coerce path to start from provided q_start;
    single_layer_nodes.clear();
    node = q_start;
    single_layer_nodes.push_back(node);
    path_options.push_back(single_layer_nodes);   

    std::vector<Vectorq7x1> q_solns;
    p_des = p_start;

    for (int istep=1;istep<nsteps;istep++) 
    {
            p_des += dp_vec;
            a_tool_des.translation() = p_des;
            cout<<"trying: "<<p_des.transpose()<<endl;
            nsolns = baxter_IK_solver_.ik_solve_approx_wrt_torso(a_tool_des, q_solns);
            std::cout<<"nsolns = "<<nsolns<<endl;
            single_layer_nodes.clear();
            if (nsolns>0) {
                single_layer_nodes.resize(nsolns);
                for (int isoln = 0; isoln < nsolns; isoln++) {
                    // this is annoying: can't treat std::vector<Vectorq7x1> same as std::vector<Eigen::VectorXd> 
                    node = q_solns[isoln];
                    single_layer_nodes[isoln] = node;
                }

                path_options.push_back(single_layer_nodes);
            }
            else {
                return false;
            }

    }

    //plan a path through the options:
    int nlayers = path_options.size();
    if (nlayers < 1) {
        ROS_WARN("no viable options: quitting");
        return false; // give up if no options
    }
    //std::vector<Eigen::VectorXd> optimal_path;
    optimal_path.resize(nlayers);
    double trip_cost;
    // set joint penalty weights for computing optimal path in joint space
    Eigen::VectorXd weights;
    weights.resize(VECTOR_DIM);
    for (int i = 0; i < VECTOR_DIM; i++) {
        weights(i) = 1.0;
    }

    // compute min-cost path, using Stagecoach algorithm
    cout << "instantiating a JointSpacePlanner:" << endl;
    { //limit the scope of jsp here:
        JointSpacePlanner jsp(path_options, weights);
        cout << "recovering the solution..." << endl;
        jsp.get_soln(optimal_path);
        trip_cost = jsp.get_trip_cost();
    }

    //now, jsp is deleted, but optimal_path lives on:
    cout << "resulting solution path: " << endl;
    for (int ilayer = 0; ilayer < nlayers; ilayer++) {
        cout << "ilayer: " << ilayer << " node: " << optimal_path[ilayer].transpose() << endl;
    }
    cout << "soln min cost: " << trip_cost << endl;
    return true;
}

// alt version: specify start as a q_vec, and goal as a Cartesian pose (w/rt torso)--but only plan a wrist path
bool CartTrajPlanner::cartesian_path_planner_wrist(Vectorq7x1 q_start,Eigen::Affine3d a_tool_end, std::vector<Eigen::VectorXd> &optimal_path) {
    std::vector<std::vector<Eigen::VectorXd> > path_options;
    path_options.clear();
    std::vector<Eigen::VectorXd> single_layer_nodes;
    Eigen::VectorXd node;
    Eigen::Affine3d a_tool_des,a_tool_start;
    Eigen::Matrix3d R_des = a_tool_end.linear();
    a_tool_start = baxter_fwd_solver_.fwd_kin_solve_wrt_torso(q_start);
    cout<<"fwd kin from q_start: "<<a_tool_start.translation().transpose()<<endl;
    cout<<"fwd kin from q_start R: "<<endl;
    cout<<a_tool_start.linear()<<endl;
    
    a_tool_start.linear() = R_des; // override the orientation component--require point down    
    a_tool_des.linear() = R_des;

    int nsolns;
    bool reachable_proposition;
    int nsteps = 0;
    Eigen::Vector3d p_des,dp_vec,del_p,p_start,p_end;
    p_start = a_tool_start.translation();
    p_end = a_tool_end.translation();
    del_p = p_end-p_start;
    cout<<"p_start: "<<p_start.transpose()<<endl;
    cout<<"p_end: "<<p_end.transpose()<<endl;
    cout<<"del_p: "<<del_p.transpose()<<endl;
    double dp_scaler = 0.05;
    nsteps = round(del_p.norm()/dp_scaler);
    if (nsteps<1) nsteps=1;
    dp_vec = del_p/nsteps;
    cout<<"dp_vec for nsteps = "<<nsteps<<" is: "<<dp_vec.transpose()<<endl;
    nsteps++; //account for pose at step 0

    //coerce path to start from provided q_start;
    single_layer_nodes.clear();
    node = q_start;
    single_layer_nodes.push_back(node);
    path_options.push_back(single_layer_nodes);   

    std::vector<Vectorq7x1> q_solns;
    p_des = p_start;

    for (int istep=1;istep<nsteps;istep++) 
    {
            p_des += dp_vec;
            a_tool_des.translation() = p_des;
            cout<<"trying: "<<p_des.transpose()<<endl;
            //int ik_wristpt_solve_approx_wrt_torso(Eigen::Affine3d const& desired_hand_pose_wrt_torso,std::vector<Vectorq7x1> &q_solns); 
            nsolns = baxter_IK_solver_.ik_wristpt_solve_approx_wrt_torso(a_tool_des, q_solns);
            std::cout<<"nsolns = "<<nsolns<<endl;
            single_layer_nodes.clear();
            if (nsolns>0) {
                single_layer_nodes.resize(nsolns);
                for (int isoln = 0; isoln < nsolns; isoln++) {
                    // this is annoying: can't treat std::vector<Vectorq7x1> same as std::vector<Eigen::VectorXd> 
                    node = q_solns[isoln];
                    single_layer_nodes[isoln] = node;
                }

                path_options.push_back(single_layer_nodes);
            }
            else {
                return false;
            }

    }

    //plan a path through the options:
    int nlayers = path_options.size();
    if (nlayers < 1) {
        ROS_WARN("no viable options: quitting");
        return false; // give up if no options
    }
    //std::vector<Eigen::VectorXd> optimal_path;
    optimal_path.resize(nlayers);
    double trip_cost;
    // set joint penalty weights for computing optimal path in joint space
    Eigen::VectorXd weights;
    weights.resize(VECTOR_DIM);
    for (int i = 0; i < VECTOR_DIM; i++) {
        weights(i) = 1.0;
    }

    // compute min-cost path, using Stagecoach algorithm
    cout << "instantiating a JointSpacePlanner:" << endl;
    { //limit the scope of jsp here:
        JointSpacePlanner jsp(path_options, weights);
        cout << "recovering the solution..." << endl;
        jsp.get_soln(optimal_path);
        trip_cost = jsp.get_trip_cost();
    }

    //now, jsp is deleted, but optimal_path lives on:
    cout << "resulting solution path: " << endl;
    for (int ilayer = 0; ilayer < nlayers; ilayer++) {
        cout << "ilayer: " << ilayer << " node: " << optimal_path[ilayer].transpose() << endl;
    }
    cout << "soln min cost: " << trip_cost << endl;
    return true;
}

