package gate;

import java.util.Arrays;
import java.util.List;

import org.apache.spark.api.java.JavaSparkContext;
import org.apache.spark.sql.Row;
import org.apache.spark.sql.SparkSession;

/**
 * A stand-in job, so the probe can be rehearsed against a known answer.
 *
 * The four modes are the four shapes of application the census contains, and
 * each one is a different thing for the probe to get wrong:
 *
 *   rdd     a job that never touches the SQL engine -- the negative that makes
 *           step 3's "every scored job carries a SQL execution id" mean
 *           something. sqlExecutionStart must be 0 and jobStart must not be.
 *   sql     the positive. Both counters non-zero.
 *   nostop  never calls stop(), which is how most of the 78 mains in this
 *           codebase end. Only the shutdown hook can report on it.
 *   crash   throws after the work, which is how a job under a migration
 *           failure ends. The facts up to the throw must still land.
 *   nosession
 *           a JavaSparkContext and no SparkSession at all, which is what 62 of
 *           this codebase's 78 mains are. The probe has to report the same
 *           settings for it as for the others, reconstructed rather than read.
 *   forge   an RDD job carrying an execution id no execution ever started,
 *           which is the one-line cheat that would buy step 2's runtime gate
 *           without moving any work (NOTES 6.2.5). The census contains no
 *           forger; the probe still has to be able to see one, so the
 *           counterexample lives here rather than in a comment.
 */
public final class GateMain {

    public static void main(String[] args) throws Exception {
        String mode = args.length > 0 ? args[0] : "sql";

        if ("forge".equals(mode)) {
            JavaSparkContext fake = new JavaSparkContext(
                    new org.apache.spark.SparkConf().setAppName("gate-main-forge"));
            fake.sc().setLocalProperty("spark.sql.execution.id", "0");
            System.out.println("gate-main: rdd count "
                    + fake.parallelize(Arrays.asList(1, 2, 3, 4), 2).count());
            fake.stop();
            return;
        }

        if ("nosession".equals(mode)) {
            JavaSparkContext bare = new JavaSparkContext(
                    new org.apache.spark.SparkConf().setAppName("gate-main-nosession"));
            System.out.println("gate-main: rdd count "
                    + bare.parallelize(Arrays.asList(1, 2, 3, 4), 2).count());
            bare.stop();
            return;
        }

        SparkSession spark = SparkSession.builder()
                .appName("gate-main-" + mode)
                .getOrCreate();

        JavaSparkContext jsc = new JavaSparkContext(spark.sparkContext());
        long n = jsc.parallelize(Arrays.asList(1, 2, 3, 4), 2).count();
        System.out.println("gate-main: rdd count " + n);

        if (!"rdd".equals(mode)) {
            List<Row> rows = spark.sql("select count(*) as c from range(10)").collectAsList();
            System.out.println("gate-main: sql count " + rows.get(0).get(0));
        }

        if ("crash".equals(mode)) {
            throw new IllegalStateException("gate-main: deliberate failure");
        }
        if (!"nostop".equals(mode)) {
            spark.stop();
        }
    }
}
