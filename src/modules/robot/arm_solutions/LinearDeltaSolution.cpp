#include "LinearDeltaSolution.h"
#include "checksumm.h"
#include "ConfigValue.h"
#include "libs/Kernel.h"
#include "libs/nuts_bolts.h"
#include "libs/Config.h"

#include <fastmath.h>
#include "Vector3.h"


#define arm_length_checksum         CHECKSUM("arm_length")
#define arm_radius_checksum         CHECKSUM("arm_radius")

#define tower1_offset_checksum      CHECKSUM("delta_tower1_offset")
#define tower2_offset_checksum      CHECKSUM("delta_tower2_offset")
#define tower3_offset_checksum      CHECKSUM("delta_tower3_offset")
#define tower1_angle_checksum       CHECKSUM("delta_tower1_angle")
#define tower2_angle_checksum       CHECKSUM("delta_tower2_angle")
#define tower3_angle_checksum       CHECKSUM("delta_tower3_angle")

#define SQ(x) powf(x, 2)
#define ROUND(x, y) (roundf(x * (float)(1e ## y)) / (float)(1e ## y))
#define PIOVER180   0.01745329251994329576923690768489F

LinearDeltaSolution::LinearDeltaSolution(Config* config)
{
    // arm_length is the length of the arm from hinge to hinge
    arm_length = config->value(arm_length_checksum)->by_default(250.0f)->as_number();
    // arm_radius is the horizontal distance from hinge to hinge when the effector is centered
    arm_radius = config->value(arm_radius_checksum)->by_default(124.0f)->as_number();

    tower1_angle = config->value(tower1_angle_checksum)->by_default(0.0f)->as_number();
    tower2_angle = config->value(tower2_angle_checksum)->by_default(0.0f)->as_number();
    tower3_angle = config->value(tower3_angle_checksum)->by_default(0.0f)->as_number();
    tower1_offset = config->value(tower1_offset_checksum)->by_default(0.0f)->as_number();
    tower2_offset = config->value(tower2_offset_checksum)->by_default(0.0f)->as_number();
    tower3_offset = config->value(tower3_offset_checksum)->by_default(0.0f)->as_number();
    
    // Tower lean support (TODO)
//    float up_vector[3] = { 0, 0, 1 };
//    std::memcpy(tower1_vector, up_vector, sizeof(tower1_vector));
//    std::memcpy(tower2_vector, up_vector, sizeof(tower2_vector));
//    std::memcpy(tower3_vector, up_vector, sizeof(tower3_vector));

    init();
}

void LinearDeltaSolution::init() {

    arm_length_squared = SQ(arm_length);

    // Effective X/Y positions of the three vertical towers.
    float delta_radius = arm_radius;

    delta_tower1_x = (delta_radius + tower1_offset) * cosf((210.0F + tower1_angle) * PIOVER180); // front left tower
    delta_tower1_y = (delta_radius + tower1_offset) * sinf((210.0F + tower1_angle) * PIOVER180);
    delta_tower2_x = (delta_radius + tower2_offset) * cosf((330.0F + tower2_angle) * PIOVER180); // front right tower
    delta_tower2_y = (delta_radius + tower2_offset) * sinf((330.0F + tower2_angle) * PIOVER180);
    delta_tower3_x = (delta_radius + tower3_offset) * cosf((90.0F  + tower3_angle) * PIOVER180); // back middle tower
    delta_tower3_y = (delta_radius + tower3_offset) * sinf((90.0F  + tower3_angle) * PIOVER180);

}

// Inverse kinematics (translates Cartesian XYZ coordinates for the effector, into carriage positions)
void LinearDeltaSolution::cartesian_to_actuator(const float cartesian_mm[], float actuator_mm[] )
{

    // Here, we answer the following question:
    // If we want the effector to be somewhere, how far does each carriage need to be from the effector?

    actuator_mm[ALPHA_STEPPER] = sqrtf(arm_length_squared
                                       - SQ(delta_tower1_x - cartesian_mm[X_AXIS])
                                       - SQ(delta_tower1_y - cartesian_mm[Y_AXIS])
                                      ) + cartesian_mm[Z_AXIS];
    actuator_mm[BETA_STEPPER ] = sqrtf(arm_length_squared
                                       - SQ(delta_tower2_x - cartesian_mm[X_AXIS])
                                       - SQ(delta_tower2_y - cartesian_mm[Y_AXIS])
                                      ) + cartesian_mm[Z_AXIS];
    actuator_mm[GAMMA_STEPPER] = sqrtf(arm_length_squared
                                       - SQ(delta_tower3_x - cartesian_mm[X_AXIS])
                                       - SQ(delta_tower3_y - cartesian_mm[Y_AXIS])
                                      ) + cartesian_mm[Z_AXIS];

}

// Forward kinematics (translates carriage positions into Cartesian XYZ)
// At the time of writing, nothing appears to call this method for any reason except ComprehensiveDeltaStrategy (see ZProbe module)
void LinearDeltaSolution::actuator_to_cartesian(const float actuator_mm[], float cartesian_mm[] )
{

    // Here, we answer the following question:
    // If the actuators are in the given positions, where will the effector be?

    // from http://en.wikipedia.org/wiki/Circumscribed_circle#Barycentric_coordinates_from_cross-_and_dot-products
    // based on https://github.com/ambrop72/aprinter/blob/2de69a/aprinter/printer/DeltaTransform.h#L81

    // Tower locations & carriage (actuator) positions
    // Current code:
    Vector3 tower1( delta_tower1_x, delta_tower1_y, actuator_mm[0] );
    Vector3 tower2( delta_tower2_x, delta_tower2_y, actuator_mm[1] );
    Vector3 tower3( delta_tower3_x, delta_tower3_y, actuator_mm[2] );

/*
    // Preliminary support for tower lean.
    // This is commented out becasue get_tower_xyz_for_dist() isn't finished.
    // When finished, this code will replace the previous block of 3 lines (Vector3 tower1... etc.)
    float xyz[3];
    
    // X tower
    get_tower_xyz_for_dist(X, xyz, actuator_mm[X]);
    Vector3 tower1( xyz[X], xyz[Y], xyz[Z] );

    // Y tower
    get_tower_xyz_for_dist(Y, xyz, actuator_mm[Y]);
    Vector3 tower2( xyz[X], xyz[Y], xyz[Z] );

    // Z tower
    get_tower_xyz_for_dist(Z, xyz, actuator_mm[Z]);
    Vector3 tower3( xyz[X], xyz[Y], xyz[Z] );
*/

    // Median points between carriages
    Vector3 s12 = tower1.sub(tower2);
    Vector3 s23 = tower2.sub(tower3);
    Vector3 s13 = tower1.sub(tower3);

    // Normal of the circle defined by the carriage positions
    Vector3 normal = s12.cross(s23);

    // Find the center of that circle
    float magsq_s12 = s12.magsq();
    float magsq_s23 = s23.magsq();
    float magsq_s13 = s13.magsq();

    float inv_nmag_sq = 1.0F / normal.magsq();
    float q = 0.5F * inv_nmag_sq;

    float a = q * magsq_s23 * s12.dot(s13);
    float b = q * magsq_s13 * s12.dot(s23) * -1.0F; // negate because we use s12 instead of s21
    float c = q * magsq_s12 * s13.dot(s23);

    Vector3 circumcenter( delta_tower1_x * a + delta_tower2_x * b + delta_tower3_x * c,
                          delta_tower1_y * a + delta_tower2_y * b + delta_tower3_y * c,
                          actuator_mm[0] * a + actuator_mm[1] * b + actuator_mm[2] * c );

    // Radius
    float r_sq = 0.5F * q * magsq_s12 * magsq_s23 * magsq_s13;

    // Finally, multiply the circle's normal by the distance to each tower and subtract from circumcenter
    float dist = sqrtf(inv_nmag_sq * (arm_length_squared - r_sq));
    Vector3 cartesian = circumcenter.sub(normal.mul(dist));

    // I guess we hate long floating point numbers, so round it to four decimal places :)
    cartesian_mm[0] = ROUND(cartesian[0], 4);
    cartesian_mm[1] = ROUND(cartesian[1], 4);
    cartesian_mm[2] = ROUND(cartesian[2], 4);

}

// If the carriage is dist mm away from 0, what are its coordinates, adjusted for tower lean?
// Disabled because it hasn't been fully implemented yet
/*
void LinearDeltaSolution::get_tower_xyz_for_dist(uint8_t tower, float xyz[], float dist)
{

    float *vec;

    switch(tower) {

        case X:
            xyz[X] = delta_tower1_x;
            xyz[Y] = delta_tower1_y;
            vec = tower1_vector;
            break;

        case Y:
            xyz[X] = delta_tower2_x;
            xyz[Y] = delta_tower2_y;
            vec = tower2_vector;
            break;

        case Z:
            xyz[X] = delta_tower3_x;
            xyz[Y] = delta_tower3_y;
            vec = tower3_vector;
            break;

        default:
            // Would be nice to print an error here, but it would flood the serial line and lock up the board.
            // Whatever you do with the rest of the code, make sure the instruction pointer never winds up here.
            xyz[X] = 100;  // 100 seems less dangerous than whatever random noise is in xyz[]
            xyz[Y] = 100;  // Should result in very strange motion, which should alert developer that there's a problem
            return;

    } // switch(tower)

    xyz[X] += vec[X] * dist;
    xyz[Y] += vec[Y] * dist;
    xyz[Z] = vec[Z] * dist;

}
*/

bool LinearDeltaSolution::set_optional(const arm_options_t& options) {

    for(auto &i : options) {
        switch(i.first) {
            case 'L': arm_length= i.second; break;
            case 'R': arm_radius= i.second; break;
            case 'A': tower1_offset = i.second; break;
            case 'B': tower2_offset = i.second; break;
            case 'C': tower3_offset = i.second; break;
            case 'D': tower1_angle = i.second; break;
            case 'E': tower2_angle = i.second; break;
            case 'F': tower3_angle = i.second; break;
            
            // Already taken or invalid: R, T, Z
            
            /*
            // TODO: This block is for tower lean support.
            case 'H': tower1_vector[X] = i.second; break;
            case 'I': tower1_vector[Y] = i.second; break;
            case 'J': tower1_vector[Z] = i.second; break; // R is already taken
            case 'N': tower2_vector[X] = i.second; break; // T is already taken
            case 'O': tower2_vector[Y] = i.second; break;
            case 'P': tower2_vector[Z] = i.second; break;
            case 'W': tower3_vector[X] = i.second; break;
            case 'X': tower3_vector[Y] = i.second; break;
            case 'Y': tower3_vector[Z] = i.second; break; // Z is already taken
            */

        }
    }
    init();
    return true;
}

bool LinearDeltaSolution::get_optional(arm_options_t& options, bool force_all) {

    // Always return these
    options['L']= arm_length;
    options['R']= arm_radius;

    // Don't return these if none of them are set
    if(force_all || (tower1_offset != 0.0F || tower2_offset != 0.0F || tower3_offset != 0.0F ||
       tower1_angle != 0.0F  || tower2_angle != 0.0F  || tower3_angle != 0.0F) ) {

        options['A'] = tower1_offset;
        options['B'] = tower2_offset;
        options['C'] = tower3_offset;
        options['D'] = tower1_angle;
        options['E'] = tower2_angle;
        options['F'] = tower3_angle;

        /*
        // TODO
        options['H'] = tower1_vector[X];
        options['I'] = tower1_vector[Y];
        options['J'] = tower1_vector[Z];
        options['N'] = tower2_vector[X];
        options['O'] = tower2_vector[Y];
        options['P'] = tower2_vector[Z];
        options['W'] = tower3_vector[X];
        options['X'] = tower3_vector[Y];
        options['Y'] = tower3_vector[Z];
        */

    }

    return true;
};
