package redis;

import java.util.*;

/**
 * Geospatial commands: a port of redis src/geo.c, src/geohash.c and
 * src/geohash_helper.c (8.0.6).
 *
 * A geo set is an ordinary sorted set whose scores are the 52 bit interleaved
 * geohash of the member's coordinates, so every sorted set command keeps
 * working on a geo key and ZSCORE returns the very same integer real redis
 * stores.
 */
public final class GeoCmds {
    private GeoCmds() {}

    /* ------------------------------------------------------------------ */
    /* geohash.h constants                                                 */
    /* ------------------------------------------------------------------ */
    private static final int GEO_STEP_MAX = 26;            /* 26*2 = 52 bits. */
    private static final double GEO_LAT_MIN = -85.05112878;
    private static final double GEO_LAT_MAX = 85.05112878;
    private static final double GEO_LONG_MIN = -180;
    private static final double GEO_LONG_MAX = 180;

    private static final int CIRCULAR_TYPE = 1;
    private static final int RECTANGLE_TYPE = 2;

    /* geohash_helper.c constants */
    private static final double D_R = Math.PI / 180.0;
    private static final double EARTH_RADIUS_IN_METERS = 6372797.560856;
    private static final double MERCATOR_MAX = 20037726.37;

    private static final Key EMPTY_KEY = new Key(new byte[0]);

    private static double degRad(double ang) { return ang * D_R; }
    private static double radDeg(double ang) { return ang / D_R; }

    /* ------------------------------------------------------------------ */
    /* geohash.c                                                           */
    /* ------------------------------------------------------------------ */

    /** GeoHashBits */
    private static final class Bits {
        long bits;
        int step;
        Bits(long bits, int step) { this.bits = bits; this.step = step; }
        Bits copy() { return new Bits(bits, step); }
        boolean isZero() { return bits == 0 && step == 0; }
        void zero() { bits = 0; step = 0; }
    }

    /** GeoHashArea */
    private static final class Area {
        double lonMin, lonMax, latMin, latMax;
    }

    /** GeoHashNeighbors */
    private static final class Neighbors {
        Bits north, east, west, south, northEast, southEast, northWest, southWest;
    }

    /** GeoHashRadius */
    private static final class Radius {
        Bits hash;
        Area area;
        Neighbors neighbors;
    }

    /** GeoShape */
    private static final class Shape {
        int type;
        final double[] xy = new double[2];
        double conversion;
        final double[] bounds = new double[4];
        double radius;         /* CIRCULAR_TYPE */
        double height, width;  /* RECTANGLE_TYPE */
    }

    /**
     * Interleave the lower bits of x and y, so the bits of x are in the even
     * positions and the bits of y in the odd ones.
     */
    private static long interleave64(long xlo, long ylo) {
        long x = xlo & 0xFFFFFFFFL;
        long y = ylo & 0xFFFFFFFFL;

        x = (x | (x << 16)) & 0x0000FFFF0000FFFFL;
        y = (y | (y << 16)) & 0x0000FFFF0000FFFFL;

        x = (x | (x << 8)) & 0x00FF00FF00FF00FFL;
        y = (y | (y << 8)) & 0x00FF00FF00FF00FFL;

        x = (x | (x << 4)) & 0x0F0F0F0F0F0F0F0FL;
        y = (y | (y << 4)) & 0x0F0F0F0F0F0F0F0FL;

        x = (x | (x << 2)) & 0x3333333333333333L;
        y = (y | (y << 2)) & 0x3333333333333333L;

        x = (x | (x << 1)) & 0x5555555555555555L;
        y = (y | (y << 1)) & 0x5555555555555555L;

        return x | (y << 1);
    }

    /** Reverse the interleave process. */
    private static long deinterleave64(long interleaved) {
        long x = interleaved;
        long y = interleaved >>> 1;

        x = (x | (x >>> 0)) & 0x5555555555555555L;
        y = (y | (y >>> 0)) & 0x5555555555555555L;

        x = (x | (x >>> 1)) & 0x3333333333333333L;
        y = (y | (y >>> 1)) & 0x3333333333333333L;

        x = (x | (x >>> 2)) & 0x0F0F0F0F0F0F0F0FL;
        y = (y | (y >>> 2)) & 0x0F0F0F0F0F0F0F0FL;

        x = (x | (x >>> 4)) & 0x00FF00FF00FF00FFL;
        y = (y | (y >>> 4)) & 0x00FF00FF00FF00FFL;

        x = (x | (x >>> 8)) & 0x0000FFFF0000FFFFL;
        y = (y | (y >>> 8)) & 0x0000FFFF0000FFFFL;

        x = (x | (x >>> 16)) & 0x00000000FFFFFFFFL;
        y = (y | (y >>> 16)) & 0x00000000FFFFFFFFL;

        return x | (y << 32);
    }

    /** geohashEncode() */
    private static Bits geohashEncode(double lonMin, double lonMax, double latMin, double latMax,
                                      double longitude, double latitude, int step) {
        Bits hash = new Bits(0, step);
        if (step > 32 || step == 0) return hash;
        if (longitude > GEO_LONG_MAX || longitude < GEO_LONG_MIN
                || latitude > GEO_LAT_MAX || latitude < GEO_LAT_MIN) return hash;
        if (latitude < latMin || latitude > latMax || longitude < lonMin || longitude > lonMax) return hash;

        double latOffset = (latitude - latMin) / (latMax - latMin);
        double longOffset = (longitude - lonMin) / (lonMax - lonMin);

        /* convert to fixed point based on the step size */
        latOffset *= (double) (1L << step);
        longOffset *= (double) (1L << step);
        hash.bits = interleave64((long) latOffset, (long) longOffset);
        return hash;
    }

    /** geohashEncodeWGS84() */
    private static Bits geohashEncodeWGS84(double longitude, double latitude, int step) {
        return geohashEncode(GEO_LONG_MIN, GEO_LONG_MAX, GEO_LAT_MIN, GEO_LAT_MAX,
                longitude, latitude, step);
    }

    /** geohashDecode(); null mirrors the C "return 0". */
    private static Area geohashDecode(double lonMin, double lonMax, double latMin, double latMax, Bits hash) {
        if (hash.isZero()) return null;
        int step = hash.step;
        long hashSep = deinterleave64(hash.bits); /* hash = [LAT][LONG] */

        double latScale = latMax - latMin;
        double longScale = lonMax - lonMin;

        long ilato = hashSep & 0xFFFFFFFFL;  /* lat part of the deinterleaved hash */
        long ilono = hashSep >>> 32;         /* long part of the deinterleaved hash */

        Area area = new Area();
        area.latMin = latMin + (ilato * 1.0 / (double) (1L << step)) * latScale;
        area.latMax = latMin + ((ilato + 1) * 1.0 / (double) (1L << step)) * latScale;
        area.lonMin = lonMin + (ilono * 1.0 / (double) (1L << step)) * longScale;
        area.lonMax = lonMin + ((ilono + 1) * 1.0 / (double) (1L << step)) * longScale;
        return area;
    }

    private static Area geohashDecodeType(Bits hash) {
        return geohashDecode(GEO_LONG_MIN, GEO_LONG_MAX, GEO_LAT_MIN, GEO_LAT_MAX, hash);
    }

    private static void geohashMoveX(Bits hash, int d) {
        if (d == 0) return;
        long x = hash.bits & 0xaaaaaaaaaaaaaaaaL;
        long y = hash.bits & 0x5555555555555555L;
        int sh = 64 - hash.step * 2;
        long zz = 0x5555555555555555L >>> sh;
        if (d > 0) {
            x = x + (zz + 1);
        } else {
            x = x | zz;
            x = x - (zz + 1);
        }
        x &= (0xaaaaaaaaaaaaaaaaL >>> sh);
        hash.bits = (x | y);
    }

    private static void geohashMoveY(Bits hash, int d) {
        if (d == 0) return;
        long x = hash.bits & 0xaaaaaaaaaaaaaaaaL;
        long y = hash.bits & 0x5555555555555555L;
        int sh = 64 - hash.step * 2;
        long zz = 0xaaaaaaaaaaaaaaaaL >>> sh;
        if (d > 0) {
            y = y + (zz + 1);
        } else {
            y = y | zz;
            y = y - (zz + 1);
        }
        y &= (0x5555555555555555L >>> sh);
        hash.bits = (x | y);
    }

    /** geohashNeighbors() */
    private static Neighbors geohashNeighbors(Bits hash) {
        Neighbors n = new Neighbors();
        n.east = hash.copy();
        n.west = hash.copy();
        n.north = hash.copy();
        n.south = hash.copy();
        n.southEast = hash.copy();
        n.southWest = hash.copy();
        n.northEast = hash.copy();
        n.northWest = hash.copy();

        geohashMoveX(n.east, 1);
        geohashMoveY(n.east, 0);

        geohashMoveX(n.west, -1);
        geohashMoveY(n.west, 0);

        geohashMoveX(n.south, 0);
        geohashMoveY(n.south, -1);

        geohashMoveX(n.north, 0);
        geohashMoveY(n.north, 1);

        geohashMoveX(n.northWest, -1);
        geohashMoveY(n.northWest, 1);

        geohashMoveX(n.northEast, 1);
        geohashMoveY(n.northEast, 1);

        geohashMoveX(n.southEast, 1);
        geohashMoveY(n.southEast, -1);

        geohashMoveX(n.southWest, -1);
        geohashMoveY(n.southWest, -1);
        return n;
    }

    /* ------------------------------------------------------------------ */
    /* geohash_helper.c                                                    */
    /* ------------------------------------------------------------------ */

    /** geohashEstimateStepsByRadius() */
    private static int estimateStepsByRadius(double rangeMeters, double lat) {
        if (rangeMeters == 0) return 26;
        int step = 1;
        while (rangeMeters < MERCATOR_MAX) {
            rangeMeters *= 2;
            step++;
        }
        step -= 2; /* Make sure range is included in most of the base cases. */

        /* Wider range towards the poles... */
        if (lat > 66 || lat < -66) {
            step--;
            if (lat > 80 || lat < -80) step--;
        }

        if (step < 1) step = 1;
        if (step > 26) step = 26;
        return step;
    }

    /** geohashBoundingBox() */
    private static void geohashBoundingBox(Shape shape, double[] bounds) {
        double longitude = shape.xy[0];
        double latitude = shape.xy[1];
        double height = shape.conversion * (shape.type == CIRCULAR_TYPE ? shape.radius : shape.height / 2);
        double width = shape.conversion * (shape.type == CIRCULAR_TYPE ? shape.radius : shape.width / 2);

        final double latDelta = radDeg(height / EARTH_RADIUS_IN_METERS);
        final double longDeltaTop = radDeg(width / EARTH_RADIUS_IN_METERS / Math.cos(degRad(latitude + latDelta)));
        final double longDeltaBottom = radDeg(width / EARTH_RADIUS_IN_METERS / Math.cos(degRad(latitude - latDelta)));
        /* The directions of the northern and southern hemispheres are
         * opposite, so we choose different points as min/max long/lat. */
        boolean southernHemisphere = latitude < 0;
        bounds[0] = southernHemisphere ? longitude - longDeltaBottom : longitude - longDeltaTop;
        bounds[2] = southernHemisphere ? longitude + longDeltaBottom : longitude + longDeltaTop;
        bounds[1] = latitude - latDelta;
        bounds[3] = latitude + latDelta;
    }

    /** geohashCalculateAreasByShapeWGS84() */
    private static Radius calculateAreasByShape(Shape shape) {
        geohashBoundingBox(shape, shape.bounds);
        double minLon = shape.bounds[0];
        double minLat = shape.bounds[1];
        double maxLon = shape.bounds[2];
        double maxLat = shape.bounds[3];

        double longitude = shape.xy[0];
        double latitude = shape.xy[1];
        /* radius_meters is calculated differently in different search types:
         * CIRCULAR_TYPE just uses the radius, RECTANGLE_TYPE the distance from
         * the center point to the corner. */
        double radiusMeters = shape.type == CIRCULAR_TYPE ? shape.radius
                : Math.sqrt((shape.width / 2) * (shape.width / 2) + (shape.height / 2) * (shape.height / 2));
        radiusMeters *= shape.conversion;

        int steps = estimateStepsByRadius(radiusMeters, latitude);

        Bits hash = geohashEncode(GEO_LONG_MIN, GEO_LONG_MAX, GEO_LAT_MIN, GEO_LAT_MAX,
                longitude, latitude, steps);
        Neighbors neighbors = geohashNeighbors(hash);
        Area area = geohashDecode(GEO_LONG_MIN, GEO_LONG_MAX, GEO_LAT_MIN, GEO_LAT_MAX, hash);

        /* Check if the step is enough at the limits of the covered area. */
        boolean decreaseStep = false;
        {
            Area north = geohashDecodeType(neighbors.north);
            Area south = geohashDecodeType(neighbors.south);
            Area east = geohashDecodeType(neighbors.east);
            Area west = geohashDecodeType(neighbors.west);

            if (north != null && north.latMax < maxLat) decreaseStep = true;
            if (south != null && south.latMin > minLat) decreaseStep = true;
            if (east != null && east.lonMax < maxLon) decreaseStep = true;
            if (west != null && west.lonMin > minLon) decreaseStep = true;
        }

        if (steps > 1 && decreaseStep) {
            steps--;
            hash = geohashEncode(GEO_LONG_MIN, GEO_LONG_MAX, GEO_LAT_MIN, GEO_LAT_MAX,
                    longitude, latitude, steps);
            neighbors = geohashNeighbors(hash);
            area = geohashDecode(GEO_LONG_MIN, GEO_LONG_MAX, GEO_LAT_MIN, GEO_LAT_MAX, hash);
        }

        /* Exclude the search areas that are useless. */
        if (steps >= 2 && area != null) {
            if (area.latMin < minLat) {
                neighbors.south.zero();
                neighbors.southWest.zero();
                neighbors.southEast.zero();
            }
            if (area.latMax > maxLat) {
                neighbors.north.zero();
                neighbors.northEast.zero();
                neighbors.northWest.zero();
            }
            if (area.lonMin < minLon) {
                neighbors.west.zero();
                neighbors.southWest.zero();
                neighbors.northWest.zero();
            }
            if (area.lonMax > maxLon) {
                neighbors.east.zero();
                neighbors.southEast.zero();
                neighbors.northEast.zero();
            }
        }
        Radius r = new Radius();
        r.hash = hash;
        r.neighbors = neighbors;
        r.area = area;
        return r;
    }

    /** geohashAlign52Bits() */
    private static long geohashAlign52Bits(Bits hash) {
        return hash.bits << (52 - hash.step * 2);
    }

    /** geohashGetLatDistance() */
    private static double geohashGetLatDistance(double lat1d, double lat2d) {
        return EARTH_RADIUS_IN_METERS * Math.abs(degRad(lat2d) - degRad(lat1d));
    }

    /** geohashGetDistance(): haversine great circle distance. */
    private static double geohashGetDistance(double lon1d, double lat1d, double lon2d, double lat2d) {
        double lat1r, lon1r, lat2r, lon2r, u, v, a;
        lon1r = degRad(lon1d);
        lon2r = degRad(lon2d);
        v = Math.sin((lon2r - lon1r) / 2);
        /* if v == 0 we can avoid the expensive math when lons are the same */
        if (v == 0.0) return geohashGetLatDistance(lat1d, lat2d);
        lat1r = degRad(lat1d);
        lat2r = degRad(lat2d);
        u = Math.sin((lat2r - lat1r) / 2);
        a = u * u + Math.cos(lat1r) * Math.cos(lat2r) * v * v;
        return 2.0 * EARTH_RADIUS_IN_METERS * Math.asin(Math.sqrt(a));
    }

    /** geohashGetDistanceIfInRadius() */
    private static boolean geohashGetDistanceIfInRadius(double x1, double y1, double x2, double y2,
                                                        double radius, double[] distance) {
        distance[0] = geohashGetDistance(x1, y1, x2, y2);
        return !(distance[0] > radius);
    }

    /** geohashGetDistanceIfInRectangle() */
    private static boolean geohashGetDistanceIfInRectangle(double widthM, double heightM,
                                                           double x1, double y1, double x2, double y2,
                                                           double[] distance) {
        /* latitude distance is cheaper to compute, so check it first */
        double latDistance = geohashGetLatDistance(y2, y1);
        if (latDistance > heightM / 2) return false;
        double lonDistance = geohashGetDistance(x2, y2, x1, y2);
        if (lonDistance > widthM / 2) return false;
        distance[0] = geohashGetDistance(x1, y1, x2, y2);
        return true;
    }

    /* ------------------------------------------------------------------ */
    /* geo.c helpers                                                       */
    /* ------------------------------------------------------------------ */

    /** geoPoint */
    private static final class GeoPoint {
        double longitude, latitude, dist, score;
        byte[] member;
        GeoPoint(double lon, double lat, double dist, double score, byte[] member) {
            this.longitude = lon; this.latitude = lat; this.dist = dist;
            this.score = score; this.member = member;
        }
    }

    /** decodeGeohash() */
    private static double[] decodeGeohash(double bits) {
        Bits hash = new Bits((long) bits, GEO_STEP_MAX);
        Area area = geohashDecodeType(hash);
        if (area == null) return null;
        double[] xy = new double[2];
        xy[0] = (area.lonMin + area.lonMax) / 2;
        if (xy[0] > GEO_LONG_MAX) xy[0] = GEO_LONG_MAX;
        if (xy[0] < GEO_LONG_MIN) xy[0] = GEO_LONG_MIN;
        xy[1] = (area.latMin + area.latMax) / 2;
        if (xy[1] > GEO_LAT_MAX) xy[1] = GEO_LAT_MAX;
        if (xy[1] < GEO_LAT_MIN) xy[1] = GEO_LAT_MIN;
        return xy;
    }

    /** extractLongLatOrReply(): argv[idx] is the longitude, argv[idx+1] the latitude. */
    private static void extractLongLat(Client c, int idx, double[] xy) {
        for (int i = 0; i < 2; i++) {
            Double v = Util.string2d(c.arg(idx + i));
            if (v == null) throw RedisError.notFloat();
            xy[i] = v;
        }
        if (xy[0] < GEO_LONG_MIN || xy[0] > GEO_LONG_MAX
                || xy[1] < GEO_LAT_MIN || xy[1] > GEO_LAT_MAX) {
            throw new RedisError(String.format(Locale.US,
                    "ERR invalid longitude,latitude pair %f,%f", xy[0], xy[1]));
        }
    }

    /** zsetScore(): the score of a member, or null. */
    private static Double zsetScore(RObj zobj, byte[] member) {
        if (zobj == null) return null;
        return ((RZSet) zobj.ptr).score(member);
    }

    /** longLatFromMember() */
    private static boolean longLatFromMember(RObj zobj, byte[] member, double[] xy) {
        Double score = zsetScore(zobj, member);
        if (score == null) return false;
        double[] p = decodeGeohash(score);
        if (p == null) return false;
        xy[0] = p[0];
        xy[1] = p[1];
        return true;
    }

    /** extractUnitOrReply() */
    private static double extractUnit(byte[] unit) {
        if (Util.equalsIgnoreCase(unit, "m")) return 1;
        if (Util.equalsIgnoreCase(unit, "km")) return 1000;
        if (Util.equalsIgnoreCase(unit, "ft")) return 0.3048;
        if (Util.equalsIgnoreCase(unit, "mi")) return 1609.34;
        throw new RedisError("ERR unsupported unit provided. please use M, KM, FT, MI");
    }

    /** extractDistanceOrReply() */
    private static void extractDistance(Client c, int idx, Shape shape) {
        Double distance = Util.string2d(c.arg(idx));
        if (distance == null) throw new RedisError("ERR need numeric radius");
        if (distance < 0) throw new RedisError("ERR radius cannot be negative");
        shape.radius = distance;
        shape.conversion = extractUnit(c.arg(idx + 1));
    }

    /** extractBoxOrReply() */
    private static void extractBox(Client c, int idx, Shape shape) {
        Double w = Util.string2d(c.arg(idx));
        if (w == null) throw new RedisError("ERR need numeric width");
        Double h = Util.string2d(c.arg(idx + 1));
        if (h == null) throw new RedisError("ERR need numeric height");
        if (h < 0 || w < 0) throw new RedisError("ERR height or width cannot be negative");
        shape.height = h;
        shape.width = w;
        shape.conversion = extractUnit(c.arg(idx + 2));
    }

    /**
     * addReplyDoubleDistance(): util.c fixedpoint_d2string() with 4 fractional
     * digits, that is "%.4f" with llrint()'s round-half-to-even.
     */
    private static byte[] fixedPointDistance(double d) {
        if (d == 0) return Util.b("0.0000");
        long svalue = (long) Math.rint(d * 10000.0);
        boolean neg = svalue < 0;
        long value = neg ? -svalue : svalue;
        StringBuilder sb = new StringBuilder();
        if (neg) sb.append('-');
        sb.append(value / 10000L).append('.');
        long frac = value % 10000L;
        if (frac < 1000) sb.append('0');
        if (frac < 100) sb.append('0');
        if (frac < 10) sb.append('0');
        sb.append(frac);
        return Util.b(sb.toString());
    }

    /** geoWithinShape() */
    private static boolean geoWithinShape(Shape shape, double score, double[] xy, double[] distance) {
        double[] p = decodeGeohash(score);
        if (p == null) return false;
        xy[0] = p[0];
        xy[1] = p[1];
        if (shape.type == CIRCULAR_TYPE) {
            if (!geohashGetDistanceIfInRadius(shape.xy[0], shape.xy[1], xy[0], xy[1],
                    shape.radius * shape.conversion, distance)) return false;
        } else if (shape.type == RECTANGLE_TYPE) {
            if (!geohashGetDistanceIfInRectangle(shape.width * shape.conversion,
                    shape.height * shape.conversion,
                    shape.xy[0], shape.xy[1], xy[0], xy[1], distance)) return false;
        }
        return true;
    }

    /** geoGetPointsInRange(): min inclusive, max exclusive. */
    private static void geoGetPointsInRange(RObj zobj, double min, double max, Shape shape,
                                            List<GeoPoint> ga, long limit) {
        RZSet z = (RZSet) zobj.ptr;
        for (RZSet.Entry e : z.sorted.tailSet(new RZSet.Entry(min, EMPTY_KEY), true)) {
            /* Abort when the node is no longer in range (maxex = 1). */
            if (!(e.score < max)) break;
            double[] xy = new double[2];
            double[] distance = new double[1];
            if (geoWithinShape(shape, e.score, xy, distance))
                ga.add(new GeoPoint(xy[0], xy[1], distance[0], e.score, e.member.b));
            if (!ga.isEmpty() && limit != 0 && ga.size() >= limit) break;
        }
    }

    /** membersOfGeoHashBox() + scoresOfGeoHashBox() */
    private static void membersOfGeoHashBox(RObj zobj, Bits hash, List<GeoPoint> ga, Shape shape, long limit) {
        long min = geohashAlign52Bits(hash);
        long max = geohashAlign52Bits(new Bits(hash.bits + 1, hash.step));
        geoGetPointsInRange(zobj, (double) min, (double) max, shape, ga, limit);
    }

    /** membersOfAllNeighbors(): search all eight neighbors + self geohash box. */
    private static void membersOfAllNeighbors(RObj zobj, Radius n, Shape shape, List<GeoPoint> ga, long limit) {
        Bits[] neighbors = new Bits[9];
        neighbors[0] = n.hash;
        neighbors[1] = n.neighbors.north;
        neighbors[2] = n.neighbors.south;
        neighbors[3] = n.neighbors.east;
        neighbors[4] = n.neighbors.west;
        neighbors[5] = n.neighbors.northEast;
        neighbors[6] = n.neighbors.northWest;
        neighbors[7] = n.neighbors.southEast;
        neighbors[8] = n.neighbors.southWest;

        int lastProcessed = 0;
        for (int i = 0; i < neighbors.length; i++) {
            if (neighbors[i].isZero()) continue;
            /* When a huge radius is used adjacent neighbors can be the same,
             * leading to duplicated elements: skip a range identical to the
             * previously processed one. */
            if (lastProcessed != 0
                    && neighbors[i].bits == neighbors[lastProcessed].bits
                    && neighbors[i].step == neighbors[lastProcessed].step) continue;
            if (!ga.isEmpty() && limit != 0 && ga.size() >= limit) break;
            membersOfGeoHashBox(zobj, neighbors[i], ga, shape, limit);
            lastProcessed = i;
        }
    }

    /* ------------------------------------------------------------------ */
    /* GEOADD                                                              */
    /* ------------------------------------------------------------------ */

    /** GEOADD key [CH] [NX|XX] long lat name [long2 lat2 name2 ...] */
    private static void geoadd(Client c) {
        Server s = Server.I;
        boolean xx = false, nx = false;
        int longidx = 2;

        /* Parse options. At the end 'longidx' is the argument position of the
         * longitude of the first element. */
        while (longidx < c.argc) {
            byte[] opt = c.arg(longidx);
            if (Util.equalsIgnoreCase(opt, "nx")) nx = true;
            else if (Util.equalsIgnoreCase(opt, "xx")) xx = true;
            else if (Util.equalsIgnoreCase(opt, "ch")) { /* handled by ZADD */ }
            else break;
            longidx++;
        }

        if (((c.argc - longidx) % 3) != 0 || (xx && nx)) throw RedisError.syntax();

        /* Set up the vector for calling ZADD. */
        int elements = (c.argc - longidx) / 3;
        byte[][] argv = new byte[longidx + elements * 2][];
        argv[0] = Util.b("zadd");
        for (int i = 1; i < longidx; i++) argv[i] = c.arg(i);

        for (int i = 0; i < elements; i++) {
            double[] xy = new double[2];
            extractLongLat(c, longidx + i * 3, xy);
            /* Turn the coordinates into the score of the element. */
            Bits hash = geohashEncodeWGS84(xy[0], xy[1], GEO_STEP_MAX);
            long bits = geohashAlign52Bits(hash);
            argv[longidx + i * 2] = Util.ll2string(bits);
            argv[longidx + 1 + i * 2] = c.arg(longidx + i * 3 + 2);
        }

        /* Finally call ZADD that will do the work for us. */
        long dirtyBefore = s.dirty;
        byte[][] oldArgv = c.argv;
        int oldArgc = c.argc;
        c.argv = argv;
        c.argc = argv.length;
        try {
            CommandTable.lookup("zadd").handler.run(c);
        } finally {
            c.argv = oldArgv;
            c.argc = oldArgc;
        }
        /* replaceClientCommandVector(): the replicated form is the ZADD. The
         * override is only set when the dispatcher is sure to consume it. */
        if (s.dirty != dirtyBefore) s.replOverride = argv;
    }

    /* ------------------------------------------------------------------ */
    /* GEORADIUS / GEOSEARCH                                               */
    /* ------------------------------------------------------------------ */

    private static final int SORT_NONE = 0, SORT_ASC = 1, SORT_DESC = 2;

    private static final int RADIUS_COORDS = 1 << 0;
    private static final int RADIUS_MEMBER = 1 << 1;
    private static final int RADIUS_NOSTORE = 1 << 2;
    private static final int GEOSEARCH = 1 << 3;
    private static final int GEOSEARCHSTORE = 1 << 4;

    private static void georadiusGeneric(Client c, int srcKeyIndex, int flags) {
        Server s = Server.I;
        byte[] storekey = null;
        boolean storedist = false;

        /* Look up the requested zset */
        RObj zobj = s.lookupKeyRead(c, c.arg(srcKeyIndex));
        if (zobj != null && zobj.type != RObj.ZSET) throw RedisError.wrongType();

        int baseArgs;
        Shape shape = new Shape();
        if ((flags & RADIUS_COORDS) != 0) {
            /* GEORADIUS or GEORADIUS_RO */
            baseArgs = 6;
            shape.type = CIRCULAR_TYPE;
            extractLongLat(c, 2, shape.xy);
            extractDistance(c, baseArgs - 2, shape);
        } else if ((flags & RADIUS_MEMBER) != 0 && zobj == null) {
            /* We don't have a source key, but we need to proceed with argument
             * parsing so we know which reply to use depending on STORE. */
            baseArgs = 5;
        } else if ((flags & RADIUS_MEMBER) != 0) {
            /* GEORADIUSBYMEMBER or GEORADIUSBYMEMBER_RO */
            baseArgs = 5;
            shape.type = CIRCULAR_TYPE;
            if (!longLatFromMember(zobj, c.arg(2), shape.xy))
                throw new RedisError("ERR could not decode requested zset member");
            extractDistance(c, baseArgs - 2, shape);
        } else if ((flags & GEOSEARCH) != 0) {
            /* GEOSEARCH or GEOSEARCHSTORE */
            baseArgs = 2;
            if ((flags & GEOSEARCHSTORE) != 0) {
                baseArgs = 3;
                storekey = c.arg(1);
            }
        } else {
            throw new RedisError("ERR Unknown georadius search type");
        }

        /* Discover and populate all optional parameters. */
        boolean withdist = false, withhash = false, withcoords = false;
        boolean frommember = false, fromloc = false, byradius = false, bybox = false;
        int sort = SORT_NONE;
        boolean any = false;
        long count = 0;
        if (c.argc > baseArgs) {
            int remaining = c.argc - baseArgs;
            for (int i = 0; i < remaining; i++) {
                byte[] arg = c.arg(baseArgs + i);
                if (Util.equalsIgnoreCase(arg, "withdist")) {
                    withdist = true;
                } else if (Util.equalsIgnoreCase(arg, "withhash")) {
                    withhash = true;
                } else if (Util.equalsIgnoreCase(arg, "withcoord")) {
                    withcoords = true;
                } else if (Util.equalsIgnoreCase(arg, "any")) {
                    any = true;
                } else if (Util.equalsIgnoreCase(arg, "asc")) {
                    sort = SORT_ASC;
                } else if (Util.equalsIgnoreCase(arg, "desc")) {
                    sort = SORT_DESC;
                } else if (Util.equalsIgnoreCase(arg, "count") && (i + 1) < remaining) {
                    Long v = Util.string2ll(c.arg(baseArgs + i + 1));
                    if (v == null) throw RedisError.notInt();
                    count = v;
                    if (count <= 0) throw new RedisError("ERR COUNT must be > 0");
                    i++;
                } else if (Util.equalsIgnoreCase(arg, "store") && (i + 1) < remaining
                        && (flags & RADIUS_NOSTORE) == 0 && (flags & GEOSEARCH) == 0) {
                    storekey = c.arg(baseArgs + i + 1);
                    storedist = false;
                    i++;
                } else if (Util.equalsIgnoreCase(arg, "storedist") && (i + 1) < remaining
                        && (flags & RADIUS_NOSTORE) == 0 && (flags & GEOSEARCH) == 0) {
                    storekey = c.arg(baseArgs + i + 1);
                    storedist = true;
                    i++;
                } else if (Util.equalsIgnoreCase(arg, "storedist")
                        && (flags & GEOSEARCH) != 0 && (flags & GEOSEARCHSTORE) != 0) {
                    storedist = true;
                } else if (Util.equalsIgnoreCase(arg, "frommember") && (i + 1) < remaining
                        && (flags & GEOSEARCH) != 0 && !fromloc) {
                    /* No source key: keep parsing and report the error later. */
                    if (zobj == null) {
                        frommember = true;
                        i++;
                        continue;
                    }
                    if (!longLatFromMember(zobj, c.arg(baseArgs + i + 1), shape.xy))
                        throw new RedisError("ERR could not decode requested zset member");
                    frommember = true;
                    i++;
                } else if (Util.equalsIgnoreCase(arg, "fromlonlat") && (i + 2) < remaining
                        && (flags & GEOSEARCH) != 0 && !frommember) {
                    extractLongLat(c, baseArgs + i + 1, shape.xy);
                    fromloc = true;
                    i += 2;
                } else if (Util.equalsIgnoreCase(arg, "byradius") && (i + 2) < remaining
                        && (flags & GEOSEARCH) != 0 && !bybox) {
                    extractDistance(c, baseArgs + i + 1, shape);
                    shape.type = CIRCULAR_TYPE;
                    byradius = true;
                    i += 2;
                } else if (Util.equalsIgnoreCase(arg, "bybox") && (i + 3) < remaining
                        && (flags & GEOSEARCH) != 0 && !byradius) {
                    extractBox(c, baseArgs + i + 1, shape);
                    shape.type = RECTANGLE_TYPE;
                    bybox = true;
                    i += 3;
                } else {
                    throw RedisError.syntax();
                }
            }
        }

        /* Trap options not compatible with STORE and STOREDIST. */
        if (storekey != null && (withdist || withhash || withcoords)) {
            throw new RedisError("ERR " + ((flags & GEOSEARCHSTORE) != 0
                    ? "GEOSEARCHSTORE" : "STORE option in GEORADIUS")
                    + " is not compatible with WITHDIST, WITHHASH and WITHCOORD options");
        }

        if ((flags & GEOSEARCH) != 0 && !(frommember || fromloc)) {
            throw new RedisError("ERR exactly one of FROMMEMBER or FROMLONLAT can be specified for "
                    + c.argStr(0));
        }

        if ((flags & GEOSEARCH) != 0 && !(byradius || bybox)) {
            throw new RedisError("ERR exactly one of BYRADIUS and BYBOX can be specified for "
                    + c.argStr(0));
        }

        if (any && count == 0) throw new RedisError("ERR the ANY argument requires COUNT argument");

        /* Return ASAP when the source key does not exist. */
        if (zobj == null) {
            if (storekey != null) {
                /* store key is not NULL, try to delete it and return 0. */
                if (s.dbDelete(c.db, storekey)) {
                    s.notifyKeyspaceEvent('g', "del", storekey, c.db);
                    s.dirty++;
                }
                c.addInt(0);
            } else {
                c.addEmptyArray();
            }
            return;
        }

        /* COUNT without ordering does not make much sense (we need to sort in
         * order to return the closest N entries), so force ASC ordering when
         * COUNT was specified but no sorting was requested; not for ANY. */
        if (count != 0 && sort == SORT_NONE && !any) sort = SORT_ASC;

        /* Get all neighbor geohash boxes for our radius search */
        Radius georadius = calculateAreasByShape(shape);

        /* Search the zset for all matching points */
        List<GeoPoint> ga = new ArrayList<>();
        membersOfAllNeighbors(zobj, georadius, shape, ga, any ? count : 0);

        /* If no matching results, the user gets an empty reply. */
        if (ga.isEmpty() && storekey == null) {
            c.addEmptyArray();
            return;
        }

        int resultLength = ga.size();
        int returnedItems = (count == 0 || resultLength < count) ? resultLength : (int) count;
        int optionLength = 0;

        /* Process [optional] requested sorting */
        if (sort == SORT_ASC) {
            ga.sort((a, b) -> Double.compare(a.dist, b.dist));
        } else if (sort == SORT_DESC) {
            ga.sort((a, b) -> Double.compare(b.dist, a.dist));
        }

        if (storekey == null) {
            /* No target key, return results to the user. */
            if (withdist) optionLength++;
            if (withcoords) optionLength++;
            if (withhash) optionLength++;

            c.addArray(returnedItems);
            for (int i = 0; i < returnedItems; i++) {
                GeoPoint gp = ga.get(i);
                gp.dist /= shape.conversion; /* Fix according to unit. */

                /* If we have options in optionLength, return each sub-result
                 * as a nested multi-bulk; add 1 for the result value itself. */
                if (optionLength != 0) c.addArray(optionLength + 1);

                c.addBulk(gp.member);

                if (withdist) c.addBulk(fixedPointDistance(gp.dist));
                if (withhash) c.addInt((long) gp.score);
                if (withcoords) {
                    c.addArray(2);
                    c.addDouble(gp.longitude);
                    c.addDouble(gp.latitude);
                }
            }
        } else {
            /* Target key, create a sorted set with the results. */
            RObj newz = null;
            RZSet zs = null;
            long maxelelen = 0, totelelen = 0;

            if (returnedItems != 0) {
                newz = new RObj(RObj.ZSET, RObj.ENC_SKIPLIST, new RZSet());
                zs = (RZSet) newz.ptr;
            }

            for (int i = 0; i < returnedItems; i++) {
                GeoPoint gp = ga.get(i);
                gp.dist /= shape.conversion; /* Fix according to unit. */
                double score = storedist ? gp.dist : gp.score;
                int elelen = gp.member.length;
                if (maxelelen < elelen) maxelelen = elelen;
                totelelen += elelen;
                zs.add(gp.member, score == 0.0 ? 0.0 : score);
            }

            if (returnedItems != 0) {
                /* zsetConvertToListpackIfNeeded() */
                if (zs.size() <= s.config.getLong("zset-max-listpack-entries")
                        && maxelelen <= s.config.getLong("zset-max-listpack-value"))
                    newz.encoding = RObj.ENC_LISTPACK;
                s.setKey(c, storekey, newz);
                s.notifyKeyspaceEvent('z', (flags & GEOSEARCH) != 0 ? "geosearchstore" : "georadiusstore",
                        storekey, c.db);
                s.dirty += returnedItems;
            } else if (s.dbDelete(c.db, storekey)) {
                s.notifyKeyspaceEvent('g', "del", storekey, c.db);
                s.dirty++;
            }
            c.addInt(returnedItems);
        }
    }

    /* ------------------------------------------------------------------ */
    /* GEOHASH / GEOPOS / GEODIST                                          */
    /* ------------------------------------------------------------------ */

    private static final String GEOALPHABET = "0123456789bcdefghjkmnpqrstuvwxyz";

    /** GEOHASH key ele1 ele2 ... eleN */
    private static void geohashCommand(Client c) {
        Server s = Server.I;
        RObj zobj = s.lookupKeyRead(c, c.arg(1));
        if (zobj != null && zobj.type != RObj.ZSET) throw RedisError.wrongType();

        c.addArray(c.argc - 2);
        for (int j = 2; j < c.argc; j++) {
            Double score = zsetScore(zobj, c.arg(j));
            if (score == null) {
                c.addNil();
                continue;
            }
            /* The internal format uses -85,85 as the latitude range while the
             * standard geohash algorithm uses -90,90: decode and re-encode. */
            double[] xy = decodeGeohash(score);
            if (xy == null) {
                c.addNil();
                continue;
            }
            Bits hash = geohashEncode(-180, 180, -90, 90, xy[0], xy[1], 26);
            byte[] buf = new byte[11];
            for (int i = 0; i < 11; i++) {
                int idx;
                if (i == 10) {
                    /* We have just 52 bits, but the API used to output an
                     * 11 bytes geohash. For compatibility we assume zero. */
                    idx = 0;
                } else {
                    idx = (int) ((hash.bits >>> (52 - ((i + 1) * 5))) & 0x1f);
                }
                buf[i] = (byte) GEOALPHABET.charAt(idx);
            }
            c.addBulk(buf);
        }
    }

    /** GEOPOS key ele1 ele2 ... eleN */
    private static void geopos(Client c) {
        Server s = Server.I;
        RObj zobj = s.lookupKeyRead(c, c.arg(1));
        if (zobj != null && zobj.type != RObj.ZSET) throw RedisError.wrongType();

        c.addArray(c.argc - 2);
        for (int j = 2; j < c.argc; j++) {
            Double score = zsetScore(zobj, c.arg(j));
            if (score == null) {
                c.addNilArray();
                continue;
            }
            double[] xy = decodeGeohash(score);
            if (xy == null) {
                c.addNilArray();
                continue;
            }
            c.addArray(2);
            c.addDouble(xy[0]);
            c.addDouble(xy[1]);
        }
    }

    /** GEODIST key ele1 ele2 [unit] */
    private static void geodist(Client c) {
        Server s = Server.I;
        double toMeter = 1;

        /* Check if there is the unit to extract, otherwise assume meters. */
        if (c.argc == 5) toMeter = extractUnit(c.arg(4));
        else if (c.argc > 5) throw RedisError.syntax();

        RObj zobj = s.lookupKeyRead(c, c.arg(1));
        if (zobj == null) { c.addNil(); return; }
        if (zobj.type != RObj.ZSET) throw RedisError.wrongType();

        /* Get the scores. We need both otherwise NULL is returned. */
        Double score1 = zsetScore(zobj, c.arg(2));
        Double score2 = zsetScore(zobj, c.arg(3));
        if (score1 == null || score2 == null) { c.addNil(); return; }

        double[] p1 = decodeGeohash(score1);
        double[] p2 = decodeGeohash(score2);
        if (p1 == null || p2 == null) { c.addNil(); return; }
        c.addBulk(fixedPointDistance(geohashGetDistance(p1[0], p1[1], p2[0], p2[1]) / toMeter));
    }

    /* ------------------------------------------------------------------ */
    /* registration                                                        */
    /* ------------------------------------------------------------------ */
    public static void register() {
        CommandTable.reg("geoadd", -5, "write denyoom", 1, 1, 1, GeoCmds::geoadd);
        CommandTable.reg("geodist", -4, "readonly", 1, 1, 1, GeoCmds::geodist);
        CommandTable.reg("geohash", -2, "readonly", 1, 1, 1, GeoCmds::geohashCommand);
        CommandTable.reg("geopos", -2, "readonly", 1, 1, 1, GeoCmds::geopos);
        CommandTable.reg("georadius", -6, "write denyoom movablekeys", 1, 1, 1,
                c -> georadiusGeneric(c, 1, RADIUS_COORDS));
        CommandTable.reg("georadius_ro", -6, "readonly", 1, 1, 1,
                c -> georadiusGeneric(c, 1, RADIUS_COORDS | RADIUS_NOSTORE));
        CommandTable.reg("georadiusbymember", -5, "write denyoom movablekeys", 1, 1, 1,
                c -> georadiusGeneric(c, 1, RADIUS_MEMBER));
        CommandTable.reg("georadiusbymember_ro", -5, "readonly", 1, 1, 1,
                c -> georadiusGeneric(c, 1, RADIUS_MEMBER | RADIUS_NOSTORE));
        CommandTable.reg("geosearch", -7, "readonly", 1, 1, 1,
                c -> georadiusGeneric(c, 1, GEOSEARCH));
        CommandTable.reg("geosearchstore", -8, "write denyoom", 1, 2, 1,
                c -> georadiusGeneric(c, 2, GEOSEARCH | GEOSEARCHSTORE));
    }
}
