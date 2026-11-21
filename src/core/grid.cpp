#include "grid.h"

#include <math.h>
#include <stdio.h>

namespace gismo {

namespace {

constexpr double pi = 3.14159265358979323846;
constexpr double degrees_to_radians = pi / 180.0;
constexpr double arc_seconds_to_radians = degrees_to_radians / 3600.0;

struct ellipsoid {
    double semi_major_axis;
    double semi_minor_axis;
};

constexpr ellipsoid grs80 = {6378137.000, 6356752.314140};
constexpr ellipsoid airy_1830 = {6377563.396, 6356256.909};

constexpr double helmert_x = -446.448;
constexpr double helmert_y = 125.157;
constexpr double helmert_z = -542.060;
constexpr double helmert_scale_ppm = 20.4894;
constexpr double helmert_rotation_x = -0.1502 * arc_seconds_to_radians;
constexpr double helmert_rotation_y = -0.2470 * arc_seconds_to_radians;
constexpr double helmert_rotation_z = -0.8421 * arc_seconds_to_radians;

constexpr double scale_factor = 0.9996012717;
constexpr double true_origin_latitude = 49.0 * degrees_to_radians;
constexpr double true_origin_longitude = -2.0 * degrees_to_radians;
constexpr double false_easting = 400000.0;
constexpr double false_northing = -100000.0;

struct cartesian {
    double x;
    double y;
    double z;
};

double eccentricity_squared(const ellipsoid &shape) {
    double a2 = shape.semi_major_axis * shape.semi_major_axis;
    double b2 = shape.semi_minor_axis * shape.semi_minor_axis;
    return (a2 - b2) / a2;
}

cartesian to_cartesian(double latitude, double longitude, double height, const ellipsoid &shape) {
    double e2 = eccentricity_squared(shape);
    double sin_latitude = sin(latitude);
    double cos_latitude = cos(latitude);
    double radius = shape.semi_major_axis / sqrt(1 - e2 * sin_latitude * sin_latitude);
    return {
        (radius + height) * cos_latitude * cos(longitude),
        (radius + height) * cos_latitude * sin(longitude),
        ((1 - e2) * radius + height) * sin_latitude,
    };
}

void to_geodetic(const cartesian &point, const ellipsoid &shape, double &latitude, double &longitude) {
    double e2 = eccentricity_squared(shape);
    double p = sqrt(point.x * point.x + point.y * point.y);
    longitude = atan2(point.y, point.x);
    latitude = atan2(point.z, p * (1 - e2));
    for (int iteration = 0; iteration < 10; iteration++) {
        double sin_latitude = sin(latitude);
        double radius = shape.semi_major_axis / sqrt(1 - e2 * sin_latitude * sin_latitude);
        double next = atan2(point.z + e2 * radius * sin_latitude, p);
        double change = fabs(next - latitude);
        latitude = next;
        if (change < 1e-12) {
            break;
        }
    }
}

cartesian helmert(const cartesian &point) {
    double scale = 1 + helmert_scale_ppm * 1e-6;
    return {
        helmert_x + scale * point.x - helmert_rotation_z * point.y + helmert_rotation_y * point.z,
        helmert_y + helmert_rotation_z * point.x + scale * point.y - helmert_rotation_x * point.z,
        helmert_z - helmert_rotation_y * point.x + helmert_rotation_x * point.y + scale * point.z,
    };
}

grid_position transverse_mercator(double latitude, double longitude) {
    double a = airy_1830.semi_major_axis;
    double b = airy_1830.semi_minor_axis;
    double e2 = eccentricity_squared(airy_1830);
    double n = (a - b) / (a + b);
    double n2 = n * n;
    double n3 = n2 * n;

    double sin_latitude = sin(latitude);
    double cos_latitude = cos(latitude);
    double tan_latitude = tan(latitude);
    double denominator = 1 - e2 * sin_latitude * sin_latitude;
    double nu = a * scale_factor / sqrt(denominator);
    double rho = a * scale_factor * (1 - e2) / (denominator * sqrt(denominator));
    double eta2 = nu / rho - 1;

    double latitude_difference = latitude - true_origin_latitude;
    double latitude_sum = latitude + true_origin_latitude;
    double meridional_arc = b * scale_factor * (
        (1 + n + 1.25 * n2 + 1.25 * n3) * latitude_difference
        - (3 * n + 3 * n2 + 2.625 * n3) * sin(latitude_difference) * cos(latitude_sum)
        + (1.875 * n2 + 1.875 * n3) * sin(2 * latitude_difference) * cos(2 * latitude_sum)
        - (35.0 / 24.0) * n3 * sin(3 * latitude_difference) * cos(3 * latitude_sum));

    double cos3 = cos_latitude * cos_latitude * cos_latitude;
    double cos5 = cos3 * cos_latitude * cos_latitude;
    double tan2 = tan_latitude * tan_latitude;
    double tan4 = tan2 * tan2;

    double term1 = meridional_arc + false_northing;
    double term2 = nu / 2 * sin_latitude * cos_latitude;
    double term3 = nu / 24 * sin_latitude * cos3 * (5 - tan2 + 9 * eta2);
    double term3a = nu / 720 * sin_latitude * cos5 * (61 - 58 * tan2 + tan4);
    double term4 = nu * cos_latitude;
    double term5 = nu / 6 * cos3 * (nu / rho - tan2);
    double term6 = nu / 120 * cos5 * (5 - 18 * tan2 + tan4 + 14 * eta2 - 58 * tan2 * eta2);

    double delta = longitude - true_origin_longitude;
    double delta2 = delta * delta;
    double delta3 = delta2 * delta;
    double delta4 = delta2 * delta2;
    double delta5 = delta4 * delta;
    double delta6 = delta4 * delta2;

    return {
        false_easting + term4 * delta + term5 * delta3 + term6 * delta5,
        term1 + term2 * delta2 + term3 * delta4 + term3a * delta6,
    };
}

}

grid_position wgs84_to_british_national_grid(double latitude, double longitude, double height) {
    cartesian wgs84 = to_cartesian(latitude * degrees_to_radians, longitude * degrees_to_radians, height, grs80);
    double osgb36_latitude;
    double osgb36_longitude;
    to_geodetic(helmert(wgs84), airy_1830, osgb36_latitude, osgb36_longitude);
    return transverse_mercator(osgb36_latitude, osgb36_longitude);
}

bool format_grid_reference(const grid_position &position, int digits, char *text, int text_size) {
    static const char letters[] = "ABCDEFGHJKLMNOPQRSTUVWXYZ";
    if (position.easting < 0 || position.northing < 0 || position.easting >= 700000 || position.northing >= 1300000) {
        return false;
    }
    if (digits < 2 || digits > 10 || digits % 2 != 0 || text_size < digits + 4) {
        return false;
    }
    int east_square = int(position.easting / 100000);
    int north_square = int(position.northing / 100000);
    int first = (19 - north_square) - (19 - north_square) % 5 + (east_square + 10) / 5;
    int second = (19 - north_square) * 5 % 25 + east_square % 5;

    int half = digits / 2;
    int divisor = 1;
    for (int index = half; index < 5; index++) {
        divisor *= 10;
    }
    int east = int(fmod(position.easting, 100000)) / divisor;
    int north = int(fmod(position.northing, 100000)) / divisor;
    snprintf(text, text_size, "%c%c %0*d %0*d", letters[first], letters[second], half, east, half, north);
    return true;
}

}
