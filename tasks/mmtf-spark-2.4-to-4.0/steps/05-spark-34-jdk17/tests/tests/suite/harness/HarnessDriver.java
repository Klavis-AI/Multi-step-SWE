package harness;

import java.io.InputStream;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import org.apache.spark.SparkConf;
import org.apache.spark.api.java.JavaPairRDD;
import org.apache.spark.api.java.JavaRDD;
import org.apache.spark.api.java.JavaSparkContext;
import org.apache.spark.sql.AnalysisException;
import org.apache.spark.sql.Column;
import org.apache.spark.sql.Dataset;
import org.apache.spark.sql.Encoders;
import org.apache.spark.sql.Row;
import org.apache.spark.sql.RowFactory;
import org.apache.spark.sql.types.DataTypes;
import org.apache.spark.sql.types.StructField;
import org.apache.spark.sql.types.StructType;
import org.apache.spark.sql.SparkSession;
import org.rcsb.mmtf.api.StructureDataInterface;

import scala.Tuple2;

import edu.sdsc.mmtf.spark.alignments.StructureAligner;
import edu.sdsc.mmtf.spark.datasets.AdvancedSearchDataset;
import edu.sdsc.mmtf.spark.datasets.CustomReportService;
import edu.sdsc.mmtf.spark.datasets.DrugBankDataset;
import edu.sdsc.mmtf.spark.datasets.G2SDataset;
import edu.sdsc.mmtf.spark.datasets.QuaternaryStructureDataset;
import edu.sdsc.mmtf.spark.datasets.JpredDataset;
import edu.sdsc.mmtf.spark.datasets.MyVariantDataset;
import edu.sdsc.mmtf.spark.datasets.PdbToUniProt;
import edu.sdsc.mmtf.spark.datasets.PdbjMineDataset;
import edu.sdsc.mmtf.spark.datasets.SecondaryStructureSegmentExtractor;
import edu.sdsc.mmtf.spark.datasets.SwissModelDataset;
import edu.sdsc.mmtf.spark.datasets.UniProt;
import edu.sdsc.mmtf.spark.datasets.UniProt.UniProtDataset;
import edu.sdsc.mmtf.spark.interactions.GroupInteractionExtractor;
import edu.sdsc.mmtf.spark.interactions.InteractionFilter;
import edu.sdsc.mmtf.spark.io.MmtfImporter;
import edu.sdsc.mmtf.spark.io.MmtfReader;
import edu.sdsc.mmtf.spark.io.MmtfWriter;
import edu.sdsc.mmtf.spark.io.demos.TraverseStructureHierarchy;
import edu.sdsc.mmtf.spark.mappers.StructureToInteractingResidues;
import edu.sdsc.mmtf.spark.mappers.StructureToPolymerChains;
import edu.sdsc.mmtf.spark.mappers.StructureToPolymerSequences;
import edu.sdsc.mmtf.spark.ml.DatasetBalancer;
import edu.sdsc.mmtf.spark.ml.JavaRDDToDataset;
import edu.sdsc.mmtf.spark.utils.ColumnarStructureX;
import edu.sdsc.mmtf.spark.filters.ContainsAlternativeLocations;
import edu.sdsc.mmtf.spark.filters.ContainsDSaccharideChain;
import edu.sdsc.mmtf.spark.filters.ContainsDnaChain;
import edu.sdsc.mmtf.spark.filters.ContainsGroup;
import edu.sdsc.mmtf.spark.filters.ContainsLProteinChain;
import edu.sdsc.mmtf.spark.filters.ContainsRnaChain;
import edu.sdsc.mmtf.spark.filters.ContainsSequenceRegex;
import edu.sdsc.mmtf.spark.filters.DepositionDate;
import edu.sdsc.mmtf.spark.filters.NotFilter;
import edu.sdsc.mmtf.spark.filters.ReleaseDate;
import edu.sdsc.mmtf.spark.filters.Resolution;
import edu.sdsc.mmtf.spark.filters.Rfree;
import edu.sdsc.mmtf.spark.filters.ContainsDProteinChain;
import edu.sdsc.mmtf.spark.filters.OrFilter;
import edu.sdsc.mmtf.spark.filters.PolymerComposition;
import edu.sdsc.mmtf.spark.filters.Rwork;
import edu.sdsc.mmtf.spark.filters.SecondaryStructure;
import edu.sdsc.mmtf.spark.webfilters.BlastClusters;
import edu.sdsc.mmtf.spark.webfilters.ChemicalStructureQuery;
import edu.sdsc.mmtf.spark.webfilters.CustomReportQuery;
import edu.sdsc.mmtf.spark.webfilters.SequenceSimilarity;
import edu.sdsc.mmtf.spark.webfilters.WildType;
import edu.sdsc.mmtf.spark.utils.OutlierDetector;

/**
 * Entry points the codebase does not have for itself.
 *
 * Three of the library's classes are reachable from no main() and no test in
 * the tree -- StructureToInteractingResidues and JpredDataset have no caller
 * anywhere, and ExhaustiveAligner is reachable only through a line of
 * DemoQueryVsAll that ships commented out. Measured on the baseline they are
 * 91, 43 and 46 uncovered lines, and rule 7's coverage bar exists so that
 * covered code cannot be deleted without failing the suite: a file at 0% is a
 * file an agent can delete for free. So the census grows entry points rather
 * than lowering the bar.
 *
 * This class is part of the scoring instrument, never of the corpus. It is
 * compiled against the jar the run under test produced, it lives outside
 * /app, and no step ships it to the agent (rule 6). Two consequences are
 * deliberate:
 *
 *   - It pins the public signatures it names. A migration that renames
 *     StructureToInteractingResidues' constructor breaks this compile, and the
 *     census says so loudly, which is the correct answer for a library whose
 *     API is part of the behavior being preserved.
 *   - It prints through Dataset.show() and counts, the same surfaces the demos
 *     use, so norm.py needs no new rules to read the recordings.
 *
 * Every sub-command takes its inputs from the pinned corpus and from constants
 * written here, never from the clock and never from a live service.
 */
public class HarnessDriver {

    /**
     * The URL MmtfImporter builds for itself. Repeated here only to pull the
     * same three models into the local repository layout that
     * importSwissModelRepository reads; if these two ever disagree, the
     * repository job's accessions stop matching the download job's.
     */
    private static final String SMR_URL =
            "https://swissmodel.expasy.org/repository/uniprot/";
    private static final String SMR_PROVIDER = ".pdb?provider=swissmodel";

    public static void main(String[] args) throws Exception {
        if (args.length == 0) {
            System.out.println("Usage: HarnessDriver <subcommand> [args...]");
            System.exit(1);
        }
        String sub = args[0];
        String[] rest = Arrays.copyOfRange(args, 1, args.length);
        switch (sub) {
            case "interacting":
                interacting(rest);
                break;
            case "exhaustive":
                exhaustive(rest);
                break;
            case "jpred":
                jpred(rest);
                break;
            case "uniprot":
                uniprot(rest);
                break;
            case "mmtffiles":
                mmtffiles(rest);
                break;
            case "balance":
                balance(rest);
                break;
            case "pairinteractions":
                pairinteractions(rest);
                break;
            case "siftslocal":
                siftslocal(rest);
                break;
            case "swissmodel":
                swissmodel(rest);
                break;
            case "pdbredo":
                pdbredo(rest);
                break;
            case "traverse":
                traverse(rest);
                break;
            case "columnar":
                columnar(rest);
                break;
            case "outliers":
                outliers(rest);
                break;
            case "rowtypes":
                rowtypes(rest);
                break;
            case "polymerseq":
                polymerseq(rest);
                break;
            case "segments":
                segments(rest);
                break;
            case "webfilters":
                webfilters(rest);
                break;
            case "myvariant":
                myvariant(rest);
                break;
            case "drugbank":
                drugbank(rest);
                break;
            case "localfilters":
                localfilters(rest);
                break;
            case "quatstruct":
                quatstruct(rest);
                break;
            case "chemreport":
                chemreport(rest);
                break;
            case "csvdates":
                csvdates(rest);
                break;
            case "searchds":
                searchds(rest);
                break;
            case "g2s":
                g2s(rest);
                break;
            case "advnumeric":
                advnumeric(rest);
                break;
            case "advtext":
                advtext(rest);
                break;
            case "advcompose":
                advcompose(rest);
                break;
            case "advio":
                advio(rest);
                break;
            default:
                System.out.println("HarnessDriver: unknown subcommand " + sub);
                System.exit(2);
        }
    }

    /**
     * StructureToInteractingResidues over the corpus.
     *
     * The mapper finds every residue within a cutoff of each copy of one named
     * group, so the group has to occur in the corpus or the whole job is an
     * empty dataset that covers findNeighbors() and getDistanceProfile() not at
     * all. ZN is the argument every other interaction demo in the tree reaches
     * for and the corpus was built to carry it.
     */
    private static void interacting(String[] args) throws Exception {
        String groupName = args.length > 0 ? args[0] : "ZN";
        double cutoff = args.length > 1 ? Double.parseDouble(args[1]) : 4.0;

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-interacting");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfFullPath(), sc);

        JavaRDD<Row> rows = pdb.flatMap(
                new StructureToInteractingResidues(groupName, cutoff));

        Dataset<Row> ds = JavaRDDToDataset.getDataset(rows, "structureId",
                "groupName", "groupIndex", "neighborGroup", "neighborIndex",
                "distance");
        // Sorted before it is shown: the mapper runs one partition per corpus
        // entry and the union order is the partition order, which is not the
        // property under test.
        ds = ds.sort("structureId", "groupIndex", "neighborIndex");
        System.out.println("interacting: group=" + groupName + " cutoff=" + cutoff);
        ds.printSchema();
        ds.show(40);
        System.out.println("Rows: " + ds.count());

        sc.close();
    }

    /**
     * The "exhaustive" branch of StructuralAlignmentMapper.
     *
     * DemoQueryVsAll and DemoAllVsAll both hard-code a BioJava algorithm name,
     * so ExhaustiveAligner.getAlignments is never entered. The string is the
     * literal in ExhaustiveAligner's private EXHAUSTIVE_ALGORITHMS list; it is
     * written out rather than read off a constant because the class exposes
     * none.
     */
    private static void exhaustive(String[] args) throws Exception {
        double fraction = args.length > 0 ? Double.parseDouble(args[0]) : 0.5;
        long seed = 11;

        SparkSession spark = SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-exhaustive").getOrCreate();
        JavaSparkContext sc = new JavaSparkContext(spark.sparkContext());

        JavaPairRDD<String, StructureDataInterface> targets =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfReducedPath(),
                        fraction, seed, sc)
                        .flatMapToPair(new StructureToPolymerChains());

        Dataset<Row> alignments =
                StructureAligner.getAllVsAllAlignments(targets, "exhaustive");
        alignments = alignments.sort("id");

        System.out.println("exhaustive: fraction=" + fraction + " seed=" + seed);
        alignments.printSchema();
        if (materialize("exhaustive", alignments)) {
            long count = alignments.count();
            alignments.show((int) Math.min(count, 40L));
            System.out.println("Pairs: " + count);
        }

        sc.close();
    }

    /**
     * JpredDataset over the mirrored JNet v2.3.1 archive.
     *
     * Sorted by scopID before it is shown. The class collects its ids in a
     * HashSet and walks the iterator to build the rows, so the row order is a
     * hash order: stable for a given set of strings, and completely rearranged
     * by one added id. Sorting makes a recording say what the rows are rather
     * than what the set's capacity was.
     *
     * Nulls are expected in the output and are upstream's, not the fixture's:
     * the archive carries roughly twice as many .fasta files as .dssp files, and
     * a scopID with no .dssp gets a null secondaryStructure from the map lookup.
     */
    private static void jpred(String[] args) throws Exception {
        SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-jpred").getOrCreate();

        Dataset<Row> ds = JpredDataset.getDataset().sort("scopID");
        ds.printSchema();
        if (materialize("Jpred", ds)) {
            System.out.println("Jpred rows: " + ds.count());
            System.out.println("Jpred trained: "
                    + ds.filter("trained = 'true'").count());
            System.out.println("Jpred without dssp: "
                    + ds.filter("secondaryStructure is null").count());
        }

        SparkSession.builder().getOrCreate().stop();
    }

    /**
     * UniProt.getDataset for the members no demo names.
     *
     * UniProtDemo hard-codes SWISS_PROT, so getUnirefDataset -- a second parser,
     * forty lines of it, with its own column set -- is never entered. The
     * default here is UNIREF50 for that reason.
     *
     * show() truncates. The uniref files are sorted longest cluster first, and
     * the head of uniref50 is titin: an untruncated row is a single line of tens
     * of thousands of residues, which is a recording no diff can be read from.
     */
    private static void uniprot(String[] args) throws Exception {
        String name = args.length > 0 ? args[0] : "UNIREF50";

        SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-uniprot").getOrCreate();

        Dataset<Row> ds = UniProt.getDataset(UniProtDataset.valueOf(name));
        ds.printSchema();
        ds.show(10, true);
        System.out.println("UniProt " + name + " rows: " + ds.count());
        System.out.println("UniProt " + name + " columns: "
                + Arrays.toString(ds.columns()));

        SparkSession.builder().getOrCreate().stop();
    }

    /**
     * A directory of individual .mmtf.gz files, and the only producer of one.
     *
     * MmtfWriter.writeMmtfFiles is public API that nothing in the tree calls,
     * and MmtfReader.readMmtfFiles is reachable only from ReadLocalMmtf, which
     * the census ran bare because no job produces the directory it wants -- so
     * the demo printed its usage string and the read path, the write path and
     * getFiles' recursion were all 0. One phase-1 job closes all three: this
     * writes the directory, and ReadLocalMmtf reads it in phase 2.
     *
     * The subset is the first n keys in sorted order rather than a sample,
     * because the file names this writes become the input of another job: an
     * unseeded sample there would move ReadLocalMmtf's recording too, and a
     * nondeterministic fixture is worth less than no fixture (rule 7).
     * writeMmtfFiles names each file after its key, so the corpus ids are
     * visible in the output directory and in the reader's recording.
     */
    private static void mmtffiles(String[] args) throws Exception {
        String out = args.length > 0 ? args[0] : "/tmp/prod/mmtf-files";
        int n = args.length > 1 ? Integer.parseInt(args[1]) : 8;

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-mmtffiles");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfReducedPath(), sc);

        List<String> keys = new ArrayList<>(pdb.keys().collect());
        Collections.sort(keys);
        if (keys.size() > n) {
            keys = keys.subList(0, n);
        }
        final Set<String> wanted = new HashSet<>(keys);
        JavaPairRDD<String, StructureDataInterface> subset =
                pdb.filter(t -> wanted.contains(t._1));

        MmtfWriter.writeMmtfFiles(out, sc, subset);

        System.out.println("mmtffiles: wrote " + keys.size() + " to " + out);
        System.out.println("mmtffiles: ids " + keys);

        sc.close();
    }


    /**
     * Both halves of DatasetBalancer.
     *
     * downsample has one caller in the tree (DatasetClassifier:75); upsample has
     * none, which left 20 of the class's 37 lines at zero -- a block an agent
     * can delete for free. Both take a seed and both use it, so the counts below
     * are a property of the data and the seed, not of the run.
     *
     * The label column is the experimental method read off each corpus entry.
     * It is used because it is whatever the corpus says it is: the loops under
     * test run once per distinct class, so they are exercised whether the corpus
     * turns out to carry one method or four, and nothing here has to be revised
     * when the corpus is rebuilt.
     */
    private static void balance(String[] args) throws Exception {
        long seed = args.length > 0 ? Long.parseLong(args[0]) : 1L;

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-balance");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfReducedPath(), sc);

        JavaRDD<Row> rows = pdb.map(t -> {
            String[] methods = t._2.getExperimentalMethods();
            return RowFactory.create(t._1,
                    methods.length > 0 ? methods[0] : "UNKNOWN");
        });

        Dataset<Row> data = JavaRDDToDataset.getDataset(rows, "structureId",
                "method");
        show("input", data);
        show("downsampled", DatasetBalancer.downsample(data, "method", seed));
        show("upsampled", DatasetBalancer.upsample(data, "method", seed));

        sc.close();
    }

    /**
     * The three print methods TraverseStructureHierarchy does not call itself.
     *
     * printAll() is the demo's entry point and it invokes five of the eight
     * printers; printChainGroupInfo is commented out in its body, and
     * printEntityInfo and traverse are never named at all. That is 55 of the
     * file's 56 uncovered lines, in a demo whose whole purpose is to show how
     * to read every category the MMTF API exposes -- exactly the code a
     * migration is most likely to touch and least likely to be caught deleting.
     *
     * 2ONX by default and not the demo's own 1HV4: printChainGroupInfo prints
     * eleven lines per group, and 1HV4 has 1156 of them against 2ONX's 4. The
     * lines reached are the same either way, so the recording is 40 lines
     * instead of 13000.
     */
    private static void traverse(String[] args) throws Exception {
        List<String> ids = args.length > 0 ? Arrays.asList(args)
                                           : Arrays.asList("2ONX");

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-traverse");
        JavaSparkContext sc = new JavaSparkContext(conf);

        // collected rather than foreach'd: an executor's stdout is interleaved
        // with the driver's by the time it reaches the recording, and these
        // printers emit hundreds of ordered lines.
        List<StructureDataInterface> structures = new ArrayList<>(
                MmtfReader.downloadFullMmtfFiles(ids, sc)
                          .sortByKey().values().collect());
        for (StructureDataInterface structure : structures) {
            TraverseStructureHierarchy.printEntityInfo(structure);
            TraverseStructureHierarchy.traverse(structure);
            TraverseStructureHierarchy.printChainGroupInfo(structure);
        }

        sc.close();
    }

    /**
     * The two ColumnarStructureX accessors nothing in the tree reads.
     *
     * WaterInteractions uses getNormalizedbFactors and getcAlphaCoordinates;
     * getClampedNormalizedbFactors and getcAlphaCoordinatesF have no caller,
     * which is 13 of this file's 13 uncovered lines. Printed as counts and one
     * formatted element each, because the arrays are thousands of floats long
     * and the point of the recording is that the accessor ran and agreed with
     * its own uncompressed twin.
     */
    private static void columnar(String[] args) throws Exception {
        List<String> ids = args.length > 0 ? Arrays.asList(args)
                                           : Arrays.asList("1STP", "4HHB");

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-columnar");
        JavaSparkContext sc = new JavaSparkContext(conf);

        List<Tuple2<String, StructureDataInterface>> entries =
                MmtfReader.downloadFullMmtfFiles(ids, sc).sortByKey().collect();
        for (Tuple2<String, StructureDataInterface> t : entries) {
            ColumnarStructureX cs = new ColumnarStructureX(t._2, true);
            float[] b = cs.getNormalizedbFactors();
            float[] clamped = cs.getClampedNormalizedbFactors();
            // Arrays are covariant, so the vecmath types stay out of this
            // file's imports: nothing here needs more than length and toString.
            Object[] caF = cs.getcAlphaCoordinatesF();
            Object[] caD = cs.getcAlphaCoordinates();
            int outOfRange = 0;
            for (float v : clamped) {
                if (v < -1.0f || v > 1.0f) {
                    outOfRange++;
                }
            }
            System.out.println("columnar " + t._1
                    + " atoms=" + cs.getNumAtoms()
                    + " bnorm=" + b.length
                    + " clamped=" + clamped.length
                    + " outOfRange=" + outOfRange
                    + " calphaF=" + caF.length
                    + " calphaD=" + caD.length);
            if (caF.length > 0) {
                System.out.println("  first CA (float)  " + caF[0]);
                System.out.println("  first CA (double) " + caD[0]);
            }
        }

        sc.close();
    }

    /**
     * OutlierDetector's two removal entry points and both of its guards.
     *
     * Only getLargestOutlier has a caller (WaterInteractions, via
     * removeAllOutliers' low-tail branch), so removeLargestOutliers is entirely
     * uncovered and getLargestOutlier's high-tail return at :48 never runs. The
     * arrays are written here rather than taken from the corpus because a
     * Dixon Q test is a statement about a specific set of numbers, and one
     * drawn from the corpus would change meaning the day the corpus does.
     *
     * The two IllegalArgumentExceptions are caught and printed, not allowed to
     * escape: a guard is a branch like any other, and the census scores the job
     * on its exit status.
     */
    private static void outliers(String[] args) throws Exception {
        // 3.0 is a high outlier: q2 = (3.0-1.03)/(3.0-1.0) = 0.985 > q90.
        double[] high = {1.0, 1.01, 1.02, 1.03, 3.0};
        // -2.0 is a low one, which is the branch the tree already reaches.
        double[] low = {-2.0, 1.0, 1.01, 1.02, 1.03};
        // No outlier at all: q1 and q2 are both under the limit.
        double[] flat = {1.0, 1.1, 1.2, 1.3, 1.4};

        System.out.println("outliers high  largest=" + OutlierDetector.getLargestOutlier(high, 90));
        System.out.println("outliers low   largest=" + OutlierDetector.getLargestOutlier(low, 90));
        System.out.println("outliers flat  largest=" + OutlierDetector.getLargestOutlier(flat, 90));
        System.out.println("outliers high  removeLargest=" + Arrays.toString(OutlierDetector.removeLargestOutliers(high, 90)));
        System.out.println("outliers flat  removeLargest=" + Arrays.toString(OutlierDetector.removeLargestOutliers(flat, 90)));
        System.out.println("outliers high  removeAll=" + Arrays.toString(OutlierDetector.removeAllOutliers(high, 95)));
        System.out.println("outliers high  removeAll99=" + Arrays.toString(OutlierDetector.removeAllOutliers(high, 99)));

        double[] eleven = new double[11];
        for (int i = 0; i < eleven.length; i++) {
            eleven[i] = i;
        }
        expect("array of 11", () -> OutlierDetector.getLargestOutlier(eleven, 90));
        expect("confidenceInterval 50", () -> OutlierDetector.getLargestOutlier(high, 50));
    }

    /**
     * The four schema branches of JavaRDDToDataset nothing in the tree reaches.
     *
     * Its callers build rows of String and Integer only, so LongType,
     * DoubleType and BooleanType are dead, and so is the else that prints
     * "Data type not implemented yet". That else is also a latent defect: it
     * leaves sf[i] null and hands the null straight to new StructType(...),
     * so the message is followed by a NullPointerException rather than by a
     * skipped column. Recorded here deliberately -- the branch is covered and
     * the bug is pinned in the same line of output.
     */
    private static void rowtypes(String[] args) throws Exception {
        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-rowtypes");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaRDD<Row> wide = sc.parallelize(Arrays.asList(
                RowFactory.create("a", 1, 10L, 1.5f, 2.5d, true),
                RowFactory.create("b", 2, 20L, 2.5f, 3.5d, false)), 1);
        Dataset<Row> ds = JavaRDDToDataset.getDataset(wide, "s", "i", "l", "f",
                                                      "d", "bool");
        ds.printSchema();
        ds.sort("s").show(10, false);

        expect("colNames too short", () -> JavaRDDToDataset.getDataset(wide, "s"));

        JavaRDD<Row> unsupported = sc.parallelize(Arrays.asList(
                RowFactory.create(new java.math.BigInteger("1"))), 1);
        expect("unsupported column type",
               () -> JavaRDDToDataset.getDataset(unsupported, "big"));

        sc.close();
    }

    /**
     * StructureToPolymerSequences under its second constructor.
     *
     * The tree only ever uses the no-arg form, which keys on chain name and
     * keeps duplicates; the (chainId, removeDuplicates) constructor and the
     * three lines inside call() that it switches on have no caller. 2HHB and
     * 4HHB are hemoglobins with two identical alpha and two identical beta
     * chains, so the deduplicating branch has something to drop -- with a
     * corpus of distinct sequences the flag would be covered and never taken.
     */
    private static void polymerseq(String[] args) throws Exception {
        List<String> ids = args.length > 0 ? Arrays.asList(args)
                                           : Arrays.asList("2HHB", "4HHB");

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-polymerseq");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.downloadFullMmtfFiles(ids, sc).cache();

        for (boolean chainId : new boolean[] {false, true}) {
            for (boolean dedupe : new boolean[] {false, true}) {
                List<String> keys = new ArrayList<>();
                for (Tuple2<String, String> t :
                        pdb.flatMapToPair(new StructureToPolymerSequences(
                                chainId, dedupe)).collect()) {
                    keys.add(t._1 + " len=" + t._2.length());
                }
                Collections.sort(keys);
                System.out.println("polymerseq chainId=" + chainId
                        + " dedupe=" + dedupe + " n=" + keys.size());
                for (String k : keys) {
                    System.out.println("  " + k);
                }
            }
        }

        sc.close();
    }

    /**
     * SecondaryStructureSegmentExtractor's odd-length guard.
     *
     * SecondaryStructureSegmentDemo calls it with 25, so the happy path is
     * covered and the throw at :45 is not. One line, but it is the only line
     * standing between this file and rule 7's bar, and it is a guard: an agent
     * that drops it turns a clear message into an off-by-one array walk.
     */
    private static void segments(String[] args) throws Exception {
        int good = args.length > 0 ? Integer.parseInt(args[0]) : 11;
        int bad = args.length > 1 ? Integer.parseInt(args[1]) : 10;

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-segments");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.downloadFullMmtfFiles(Arrays.asList("1STP"), sc)
                          .flatMapToPair(new StructureToPolymerChains())
                          .cache();

        Dataset<Row> ds = SecondaryStructureSegmentExtractor.getDataset(pdb, good);
        System.out.println("segments length=" + good + " n=" + ds.count());
        ds.sort("structureChainId", "sequence").show(5, false);

        expect("even segment length", () ->
               SecondaryStructureSegmentExtractor.getDataset(pdb, bad));

        sc.close();
    }

    /**
     * The two webfilters whose alternative constructors nothing in the tree
     * calls, plus both of their argument guards.
     *
     * WildTypeQuery covers WildType(boolean, int) with tags on and coverage 95;
     * SequenceSimilarityDemo covers SequenceSimilarity(String, String, double,
     * int, boolean). That leaves WildType(boolean) entirely, WildType's else
     * branch, WildType's percentSequenceCoverage guard, SequenceSimilarity's
     * defaults-only constructor, and its length guard -- 12 of the 27 lines in
     * the two files, which is what puts both of them under rule 7's bar.
     *
     * The filters are run over the corpus and the surviving ids printed rather
     * than counted. A count is one number, and four queries that happen to
     * return the same number would be one test wearing four hats; the id list
     * pins each query to the fixture that answers it, so swapping two
     * constructors shows up as a changed line and not just a changed digit.
     */
    private static void webfilters(String[] args) throws Exception {
        String sequence = args.length > 0 ? args[0]
                : "NLVQFGVMIEKMTGKSALQYNDYGCYCGIGGSHWPVDQ";

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-webfilters");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readReducedSequenceFile(sc).cache();

        // WildType(boolean): no <percentSeqAlignment> element at all, and one
        // pass per arm of the expression-tag branch.
        hits("wildtype tags=Y", pdb.filter(new WildType(true)));
        hits("wildtype tags=N", pdb.filter(new WildType(false)));

        // WildType(boolean, int) with the arm WildTypeQuery does not take. 90
        // is in sequenceCoverage, so this is the guard's pass, not its throw.
        hits("wildtype tags=N cov=90", pdb.filter(new WildType(false, 90)));

        // 42 is not one of {100, 95, 90, 85, 80, 75, 70, 65, 60}. The message
        // names the offending value and lists the legal ones, so a rewrite that
        // keeps the throw but drops the list is a changed recording.
        expect("wildtype cov=42", () -> new WildType(true, 42));

        // SequenceSimilarity(String): delegates to blast / 10.0 / 0 / true, a
        // parameter set no demo asks for and therefore its own fixture key.
        hits("seqsim defaults", pdb.filter(new SequenceSimilarity(sequence)));

        // 11 residues, one short of the minimum. Runs before any query is
        // built, so it needs no fixture.
        expect("seqsim len=11", () -> new SequenceSimilarity("ACDEFGHIKLM"));

        sc.close();
    }

    /** Surviving ids in a fixed order; RDD order is not one. */
    /**
     * The six filters no main() reaches, over the pinned corpus.
     *
     * Measured on the baseline these are 5, 5, 6, 6, 58 and 11 uncovered lines
     * on a report built from the mains alone. Each has a unit test, and rule 7
     * is explicit that the codebase's own tests count toward neither bar --
     * the agent has them, so a file they alone cover is a file that can be
     * gutted for free as long as its test is kept green. The arguments here are
     * therefore not the tests' arguments: the corpus is the pinned 82-entry
     * one rather than each test's five downloads, and every constructor arm is
     * driven rather than the one arm each test needs.
     */
    private static void localfilters(String[] args) throws Exception {
        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-localfilters");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfFullPath(), sc).cache();

        matched("altloc", pdb.filter(new ContainsAlternativeLocations()));

        // Rwork: a window that admits well-refined entries, one that admits
        // none, and one that admits everything with an Rwork at all. The
        // middle arm is the interesting one -- a filter that silently starts
        // matching nothing reads as a corpus change unless an empty answer is
        // also recorded as an answer.
        matched("rwork 0.0-0.2", pdb.filter(new Rwork(0.0, 0.2)));
        matched("rwork 0.9-1.0", pdb.filter(new Rwork(0.9, 1.0)));
        matched("rwork 0.0-1.0", pdb.filter(new Rwork(0.0, 1.0)));

        matched("dprotein", pdb.filter(new ContainsDProteinChain()));
        matched("dprotein exclusive", pdb.filter(new ContainsDProteinChain(true)));

        // OrFilter is a two-line disjunction, so what has to be recorded is
        // that it is a union rather than either side: both arms are recorded
        // above, and this line is their union or the class is wrong.
        matched("or(dprotein, rwork 0.0-0.2)",
                pdb.filter(new OrFilter(new ContainsDProteinChain(),
                                        new Rwork(0.0, 0.2))));

        // SecondaryStructure's 58 lines are one loop over chains with a Q3
        // switch inside it, so the arms are reached by windows rather than by
        // constructors: helix-rich, sheet-rich, coil-rich, everything, and
        // nothing. exclusive=true takes the other branch of the per-chain
        // match, which is the one the six-argument constructor cannot reach.
        matched("ss helix>=0.5",
                pdb.filter(new SecondaryStructure(0.5, 1.0, 0.0, 1.0, 0.0, 1.0)));
        matched("ss sheet>=0.3",
                pdb.filter(new SecondaryStructure(0.0, 1.0, 0.3, 1.0, 0.0, 1.0)));
        matched("ss coil>=0.8",
                pdb.filter(new SecondaryStructure(0.0, 1.0, 0.0, 1.0, 0.8, 1.0)));
        matched("ss any",
                pdb.filter(new SecondaryStructure(0.0, 1.0, 0.0, 1.0, 0.0, 1.0)));
        matched("ss impossible",
                pdb.filter(new SecondaryStructure(0.9, 1.0, 0.9, 1.0, 0.9, 1.0)));
        matched("ss helix>=0.5 exclusive",
                pdb.filter(new SecondaryStructure(0.5, 1.0, 0.0, 1.0, 0.0, 1.0, true)));

        // PolymerComposition: all three constructors, and both of the standard
        // nucleotide vocabularies, so the varargs arm and the List arm are
        // distinguished by their answers rather than only by compiling.
        matched("composition aa20",
                pdb.filter(new PolymerComposition(PolymerComposition.AMINO_ACIDS_20)));
        matched("composition aa22 exclusive",
                pdb.filter(new PolymerComposition(true, PolymerComposition.AMINO_ACIDS_22)));
        matched("composition dna",
                pdb.filter(new PolymerComposition(false, PolymerComposition.DNA_STD_NUCLEOTIDES)));
        matched("composition rna",
                pdb.filter(new PolymerComposition(false, PolymerComposition.RNA_STD_NUCLEOTIDES)));
        matched("composition varargs ALA,GLY",
                pdb.filter(new PolymerComposition(false, "ALA", "GLY")));

        sc.close();
    }

    /**
     * QuaternaryStructureDataset over the corpus.
     *
     * 53 lines, no caller outside its own test, and no network: it reads
     * bioassembly transforms straight off the structure. Sorted before it is
     * shown, for the reason interacting() is sorted.
     */
    private static void quatstruct(String[] args) throws Exception {
        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-quatstruct");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfFullPath(), sc);

        Dataset<Row> ds = QuaternaryStructureDataset.getDataset(pdb);
        System.out.println("quatstruct: rows=" + ds.count());
        ds.printSchema();
        ds.sort("structureId", "bioAssemblyId").show(60, false);

        sc.close();
    }

    /**
     * The three web filters no main() reaches: ChemicalStructureQuery,
     * CustomReportQuery and BlastClusters.
     *
     * Every argument here is one the mirror has a *keyed* fixture for, not a
     * query that would fall through to _default -- an unrecorded query answers
     * with filler, and a filter over filler is a recording of the mirror
     * rather than of the filter. The five SMILES payloads are the five
     * ChemicalStructureQuery builds for itself, so its own XML template is what
     * produces the key: a migration that changes how that XML is built misses
     * the fixture and the answer changes, which is the correct alarm.
     */
    private static void chemreport(String[] args) throws Exception {
        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-chemreport");
        JavaSparkContext sc = new JavaSparkContext(conf);

        List<String> ids = Arrays.asList("1HYA", "2ONX", "1F27", "4QMC", "2RTL",
                "1O06", "5JDE", "5CU4", "5L6W", "5UFU", "5IHB");
        JavaPairRDD<String, StructureDataInterface> pdb = MmtfReader
                .readSequenceFile(MmtfReader.getMmtfReducedPath(), ids, sc).cache();
        System.out.println("chemreport: corpus=" + pdb.count());

        String nag = "CC(=O)NC1C(O)OC(CO)C(O)C1O";
        String biotin = "OC(=O)CCCC[C@@H]1SC[C@@H]2NC(=O)N[C@H]12";
        String biotinSulfoxide = "OC(=O)CCCC[C@H]1[C@H]2NC(=O)N[C@H]2C[S@@]1=O";

        // The one-argument constructor, which defaults to Substructure/0.
        matched("chem nag default", pdb.filter(new ChemicalStructureQuery(nag)));
        matched("chem biotin exact",
                pdb.filter(new ChemicalStructureQuery(biotin,
                        ChemicalStructureQuery.EXACT, 0)));
        matched("chem biotin substructure",
                pdb.filter(new ChemicalStructureQuery(biotin,
                        ChemicalStructureQuery.SUBSTRUCTURE, 0)));
        matched("chem biotin similar 70",
                pdb.filter(new ChemicalStructureQuery(biotin,
                        ChemicalStructureQuery.SIMILAR, 70)));
        matched("chem sulfoxide superstructure",
                pdb.filter(new ChemicalStructureQuery(biotinSulfoxide,
                        ChemicalStructureQuery.SUPERSTRUCTURE, 0)));

        // The guard, which runs before any query is built and so needs no
        // fixture. "Blah" is not one of the four query types.
        expect("chem bad type", () -> new ChemicalStructureQuery(biotin, "Blah", 0));

        // BlastClusters at every cutoff the mirror carries a cluster file for.
        // 40 is the one the test uses; the rest are the same code over a
        // different file, which is what distinguishes a reader that works from
        // one that has memorised a path.
        for (int identity : new int[] {30, 40, 50, 70, 90, 95, 100}) {
            matched("blast " + identity, pdb.filter(new BlastClusters(identity)));
        }
        // 42 is not a cutoff RCSB publishes.
        expect("blast 42", () -> new BlastClusters(42));

        // CustomReportQuery at entry level and at chain level. The chain-level
        // pass is the one that exercises concatIds(), and 5L6W is in the
        // corpus precisely because one of its chains matches and another does
        // not -- an answer that a filter applied at the wrong granularity
        // cannot produce.
        String where = "WHERE ecNo='2.7.11.1' AND source='Homo sapiens'";
        matched("report entry", pdb.filter(new CustomReportQuery(where, "ecNo", "source")));
        matched("report chain", pdb.flatMapToPair(new StructureToPolymerChains())
                .filter(new CustomReportQuery(where, "ecNo", "source")));

        sc.close();
    }

    /**
     * The types Spark infers for the csvs the repository's own readers load.
     *
     * Nothing else in this census records a column *type*. The recordings are
     * row counts, rendered tables and filter answers, so a change in how a
     * column is inferred reaches them only where a show() happens to widen or
     * narrow -- which is to say, not reliably. Spark 3.4 makes that worth
     * asking about directly: CSVOptions.preferDate arrives defaulting to true,
     * and a yyyy-MM-dd column that 3.3.4 inferred as TimestampType is inferred
     * as DateType instead. Six reads under src/main hand the engine a csv and
     * ask it to infer, and one of them -- CustomReportService:175 -- carries
     * depositionDate.
     *
     * The reads below are the repository's, not this harness's. That is the
     * whole design: a spark.read() built here would record the options this
     * file passes and answer a question about the harness, whereas going in
     * through getDataset() records what the tree's own call sites ask for. A
     * migration that leaves those call sites silent about dates prints "date"
     * where the reference prints "timestamp", and this job loses.
     *
     * Deterministic without sorting: one line per field in schema order, which
     * is the fixture's own header order. typeName() rather than
     * StructType.toString(), because the latter carries nullability and a
     * containsNull flag that none of this is about.
     */
    private static void csvdates(String[] args) throws Exception {
        SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-csvdates").getOrCreate();

        // The report the question is about. The mirror keys its fixtures on
        // the exact posted query, and the one for these three columns answers
        // structureId,pmc,pubmedId,depositionDate with yyyy-MM-dd values --
        // the only date-shaped column any recorded read in this census sees.
        Dataset<Row> report =
                CustomReportService.getDataset("pmc", "pubmedId", "depositionDate");
        types("deposition", report);
        System.out.println("csvdates deposition: rows=" + report.count());

        // Two more of the six, for contrast rather than for coverage, over
        // fixtures the drugbank and sifts recordings already use: nothing
        // here asks the mirror for anything it was not already asked for.
        //
        // The drug links are the control. Seven columns and not a date among
        // them, which is exactly why they are worth printing: a tree that had
        // broken inference outright -- every column a string, say -- would
        // move this line too, and a recording that watched only depositionDate
        // could not tell that apart from the thing it is meant to catch.
        types("druglinks", DrugBankDataset.getDrugLinks(
                DrugBankDataset.DrugGroup.APPROVED,
                "mmtf-spark-census", "not-a-real-password"));

        // The third of the six, and the one with a mixed schema: SIFTS
        // pdb_chain_uniprot has four integer columns and four string ones, so
        // this is the check that inference is happening at all rather than
        // falling back to all-string. The query is PdbjMineDatasetTest's own.
        //
        // Not UniProt.getDataset(UNIREF50), which was the first choice and is
        // measured here so nobody tries it again: that reader fetches over
        // ftp://, and by the time this subcommand reaches it Hadoop's
        // FsUrlStreamHandlerFactory has been installed by the reads above, so
        // the URL is served by FTPFileSystem instead of the JDK's own ftp
        // client and the mirror answers `File ... does not exist`. Run first
        // in a job of its own -- which is what HarnessDriver-uniref is -- the
        // same call succeeds. A recording whose answer depends on what ran
        // before it in the same JVM is not a recording, so this reader is
        // reached over https and the ftp one is left to the job that already
        // has it.
        types("pdbjmine", PdbjMineDataset.getDataset(
                "SELECT * FROM sifts.pdb_chain_uniprot LIMIT 10"));

        // The gate rather than the reader. getDataset(String) is the
        // unauthenticated overload and this is the URL getDrugLinks built
        // above; upstream answers it without credentials with a 403, so the
        // download never becomes a csv and no schema is inferred. expectKind,
        // not expect: the message is HttpURLConnection's and carries the URL.
        expectKind("csvdates drugbank unauthenticated",
                () -> DrugBankDataset.getDataset("https://www.drugbank.ca"
                        + "/releases/latest/downloads/APPROVED-structure-links"));

        // A report asked for a column the service does not publish. It does
        // not throw, and that is the finding: the endpoint answers an
        // unrecognised customReportColumns with its default one-column
        // structureId body, so the caller is handed a frame with none of the
        // columns it asked for and an inferred schema to match. The failure
        // this subcommand guards against is a silent change of type; this is
        // the same failure's other face, a silent change of shape, and the
        // inferred type printed for the column that did come back is the
        // second half of the record.
        expect("csvdates report unpublished column",
                () -> types("unpublished",
                        CustomReportService.getDataset("notAColumn")));

        SparkSession.builder().getOrCreate().stop();
    }

    /** One line per field of a dataset's schema, in the schema's own order. */
    private static void types(String label, Dataset<Row> ds) {
        for (StructField field : ds.schema().fields()) {
            System.out.println("csvdates " + label + ": " + field.name()
                    + " " + field.dataType().typeName());
        }
    }

    /**
     * AdvancedSearchDataset, whose 22 lines are three branches and no test.
     *
     * getDataset() decides entry / entity / ligand from the *length of the
     * first result*, so the three queries below are chosen for the shape of
     * their recorded answers rather than for their biology: a stoichiometry
     * query answers with four-character entry ids, a tree-entity query with
     * "1ABC:1" entity ids, and a ligand-targeted SMILES query with
     * three-character ligand ids. Each is a keyed mirror fixture.
     *
     * The entity branch additionally joins against PdbjMineDataset's
     * entity_poly mapping, so that fixture has to agree with this one; it is
     * the only branch where two recorded services meet.
     */
    private static void searchds(String[] args) throws Exception {
        SparkSession spark = SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-searchds").getOrCreate();

        String entryQuery =
                "<orgPdbQuery>"
                + "<queryType>org.pdb.query.simple.StoichiometryQuery</queryType>"
                + "<stoichiometry>A3B3C3</stoichiometry>"
                + "</orgPdbQuery>";
        String entityQuery =
                "<orgPdbQuery>"
                + "<queryType>org.pdb.query.simple.TreeEntityQuery</queryType>"
                + "<t>1</t>"
                + "<n>9606</n>"
                + "</orgPdbQuery>";
        String ligandQuery =
                "<orgPdbQuery>"
                + "<queryType>org.pdb.query.simple.ChemSmilesQuery</queryType>"
                + "<smiles>CC(C)C1=C(Br)C(=O)C(C)=C(Br)C1=O</smiles>"
                + "<target>Ligand</target>"
                + "<searchType>Substructure</searchType>"
                + "<polymericType>Any</polymericType>"
                + "</orgPdbQuery>";

        showSearch("entry", entryQuery);
        showSearch("entity", entityQuery);
        showSearch("ligand", ligandQuery);

        spark.close();
    }

    private static void showSearch(String label, String xml) throws Exception {
        Dataset<Row> ds = AdvancedSearchDataset.getDataset(xml);
        if (ds == null) {
            // A query with no hits returns null rather than an empty Dataset.
            // Recorded as an answer, because "null" and "0 rows" are different
            // behaviors and a migration may turn one into the other.
            System.out.println("searchds " + label + ": null");
            return;
        }
        System.out.println("searchds " + label + ": rows=" + ds.count()
                + " columns=" + String.join(",", ds.columns()));
        ds.printSchema();
        ds.sort(ds.columns()[0]).show(30, false);
    }

    /**
     * G2SDataset's four entry points, of which the census reaches one.
     *
     * The variation id and the two (structure, chain) pairs are the ones the
     * g2s.genomenexus.org fixtures are keyed on. 1STP is in the set on
     * purpose: the service has no alignment for it, getPositionDataset returns
     * *null*, and MutationToStructureDemo's recorded crash (2.5 #3) is that
     * null being dereferenced. Recording the null here is what makes that crash
     * a property of the demo rather than of the dataset.
     */
    private static void g2s(String[] args) throws Exception {
        SparkSession spark = SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-g2s").getOrCreate();

        List<String> variants = Arrays.asList("chr7:g.140449098T>C");

        showG2S("position 3TV4.A", G2SDataset.getPositionDataset(variants, "3TV4", "A"));
        showG2S("full 3TV4.A", G2SDataset.getFullDataset(variants, "3TV4", "A"));
        showG2S("position 1STP.A", G2SDataset.getPositionDataset(variants, "1STP", "A"));
        showG2S("position all", G2SDataset.getPositionDataset(variants));
        showG2S("full all", G2SDataset.getFullDataset(variants));

        spark.close();
    }

    private static void showG2S(String label, Dataset<Row> ds) {
        if (ds == null) {
            System.out.println("g2s " + label + ": null");
            return;
        }
        System.out.println("g2s " + label + ": rows=" + ds.count()
                + " columns=" + String.join(",", ds.columns()));
        ds.printSchema();
        // Sorted on every column, because no single column of an alignment
        // table is unique and a partial sort leaves ties in partition order.
        Column[] cols = new Column[ds.columns().length];
        for (int i = 0; i < cols.length; i++) {
            cols[i] = ds.col(ds.columns()[i]);
        }
        ds.sort(cols).show(40, false);
    }

    /**
     * hits() with a prefix of its own. The recordings r18 and r19 carry
     * webfilters' output under the literal "webfilters "; changing that helper
     * to take a prefix would have rewritten a recording to add a caller, which
     * is the one edit a reference recording may not take.
     */
    private static void matched(String label,
            JavaPairRDD<String, StructureDataInterface> rdd) {
        List<String> ids = new ArrayList<>(rdd.keys().collect());
        Collections.sort(ids);
        System.out.println("filter " + label + ": n=" + ids.size());
        for (String id : ids) {
            System.out.println("  " + id);
        }
    }

    private static void hits(String label,
            JavaPairRDD<String, StructureDataInterface> rdd) {
        List<String> ids = new ArrayList<>(rdd.keys().collect());
        Collections.sort(ids);
        System.out.println("webfilters " + label + ": n=" + ids.size());
        for (String id : ids) {
            System.out.println("  " + id);
        }
    }

    /**
     * Third printer, for the adversarial cases: count and set on two lines.
     *
     * matched() and hits() print one line per id because their callers are
     * demonstrations -- a reader of the recording wants to see which
     * structures came back. The adv* subcommands are not demonstrations: they
     * ask ~60 questions apiece about where a filter's boundary sits, and at
     * one line per id over the 82-entry reduced corpus that is five thousand
     * lines of which the interesting content is the arithmetic between
     * neighbouring cases.
     *
     * So the ids go on one line and the count on its own. The count is the
     * assertion -- "res widest" and "res nan both" differing by one entry
     * differ by one here, and a migration that changes a comparison's
     * strictness moves a number rather than reshuffling a list -- and the id
     * line is what keeps two same-sized but different answers from comparing
     * equal, which a bare count would not.
     *
     * The prefix is "adv ", not "filter ", for the reason the class comment
     * above hits() gives: matched()'s literal is baked into recordings r18 and
     * r19, and widening a helper to take a prefix would rewrite a reference
     * recording to accommodate a new caller.
     */
    private static void counted(String label,
            JavaPairRDD<String, StructureDataInterface> rdd) {
        List<String> ids = new ArrayList<>(rdd.keys().collect());
        Collections.sort(ids);
        System.out.println("adv " + label + ": n=" + ids.size());
        if (!ids.isEmpty()) {
            System.out.println("  " + String.join(" ", ids));
        }
    }

    /**
     * Runs a call that is supposed to throw, and says which exception came
     * back.
     *
     * A guard clause is a branch, and a census that scores on exit status
     * cannot reach one by letting it escape. Printing the type and message
     * rather than just "threw" is what makes the recording able to tell an
     * IllegalArgumentException from the NullPointerException that arrives when
     * the guard is deleted.
     */
    /**
     * Run a body that is expected to reject its arguments, and record how.
     *
     * Throwable, not Exception. ChemicalStructureQuery's guard throws
     * IllegalAccessError for an unknown search type -- an Error, chosen by
     * upstream rather than by this harness -- and a catch of Exception let it
     * past, ending the job at rc=1 with the guard's message on stderr where no
     * recording reads it. The webfilters recordings are unaffected: nothing
     * they call throws an Error, so the widened catch changes no existing
     * output, which r18 and r19 are the check on.
     */
    private static void expect(String label, Body body) {
        try {
            body.run();
            System.out.println("expect " + label + ": NO EXCEPTION");
        } catch (Throwable e) {
            String msg = e.getMessage();
            System.out.println("expect " + label + ": "
                    + e.getClass().getName()
                    + (msg == null ? "" : ": " + msg.split("\n")[0]));
        }
    }

    /**
     * What expect() runs. Runnable would do for the guards reached by a plain
     * method call, but the webfilter constructors declare IOException, and a
     * lambda that cannot throw is a lambda that cannot reach them.
     */
    /**
     * Same as expect(), but records only the exception's class.
     *
     * The dividing line between the two is who wrote the message. expect()
     * prints it because in every case that uses it the message is the
     * codebase's own guard text -- ChemicalStructureQuery's "unknown search
     * type", DatasetFileConverter's "no file format specified" -- and that text
     * *is* the behavior being recorded. The adversarial reader and regex cases
     * are the other kind: the message comes from Hadoop
     * ("Input path does not exist"), from Spark ("Job aborted due to stage
     * failure: Task 0 in stage 0.0 ..."), or from java.util.regex ("Unclosed
     * group near index 1"), and none of those three are the tree under test.
     *
     * Recording them would make the expectation expire on the way up the
     * version ladder for a reason that is not a migration: Hadoop goes 2.7 to
     * 3.4 and Spark 2.4 to 4.0 between the first rung and the last, and their
     * wording is theirs to change. Rule 7 admits an adversarial input only if
     * its baseline answer is stable, and a string owned by a dependency that
     * the migration deliberately replaces is not stable by construction.
     *
     * The class is kept rather than a bare "threw", because the distinction
     * that matters here is exactly the one the class carries: an
     * InvalidInputException (rejected before the job was submitted) and a
     * SparkException (submitted, then failed in a task) are two different
     * behaviors for a reader handed a path it cannot use, and a migration that
     * turns one into the other has changed something a caller can see.
     */
    private static void expectKind(String label, Body body) {
        try {
            body.run();
            System.out.println("expect " + label + ": NO EXCEPTION");
        } catch (Throwable e) {
            System.out.println("expect " + label + ": " + e.getClass().getName());
        }
    }

    private interface Body {
        void run() throws Exception;
    }

    /** Class counts in a fixed order, which groupBy does not give on its own. */
    private static void show(String label, Dataset<Row> ds) {
        System.out.println("balance " + label + ": " + ds.count() + " row(s)");
        ds.groupBy("method").count().sort("method").show(20, false);
    }


    /**
     * The pairwise branch of the interactions module.
     *
     * interactions.GroupInteractionExtractor has two entry points and the tree
     * calls one: WaterInteractions uses getInteractions, nothing uses
     * getPairInteractions. The flag they differ by is passed straight into
     * StructureToAtomInteractions, so the unused half also leaves that mapper's
     * pairwise branch and AtomInteraction.getPairInteractionSchema uncovered.
     *
     * The filter is WaterInteractions' own, minus the parts that only matter to
     * its bridging-water post-processing: same query group, same target
     * elements, so the rows this produces are comparable with the ones the demo
     * records and a reviewer can tell at a glance which half changed.
     */
    private static void pairinteractions(String[] args) throws Exception {
        double cutoff = args.length > 0 ? Double.parseDouble(args[0]) : 3.0;

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-pairinteractions");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfReducedPath(), sc);

        InteractionFilter filter = new InteractionFilter();
        filter.setDistanceCutoff((float) cutoff);
        filter.setMinInteractions(1);
        filter.setMaxInteractions(4);
        filter.setQueryGroups(true, "HOH");
        filter.setQueryElements(true, "O");
        filter.setTargetElements(true, "O", "N", "S");

        Dataset<Row> ds = GroupInteractionExtractor.getPairInteractions(pdb, filter);
        // Sorted for the same reason as interacting(): one partition per corpus
        // entry, and the union order is not the property under test.
        ds = ds.sort(ds.columns()[0], ds.columns()[1]);
        System.out.println("pairinteractions: cutoff=" + cutoff);
        ds.printSchema();
        ds.show(20);
        System.out.println("Rows: " + ds.count());

        sc.close();
    }


    /**
     * Show the first rows, or say in one stable line that the dataset does not
     * materialize at all.
     *
     * Two of this class's sub-commands reach code that throws on the untouched
     * baseline, and both are real properties of the tree rather than of the
     * fixtures:
     *
     *   - JpredDataset builds its rows with a null secondaryStructure for every
     *     SCOP id that has a .fasta and no .dssp, and declares that column
     *     nullable=false. Upstream's own retr231.tar.gz carries 2845 .fasta
     *     against 1497 .dssp, so on real data the encoder always has a null to
     *     reject. The class is reachable from no main() and no test in the tree
     *     and has evidently never been run.
     *   - ExhaustiveAligner.getAlignments leaves x and y null unless one of the
     *     two chains is longer than the alignment, so any pair of equal-length
     *     chains -- two copies of one chain in a homodimer, say -- NPEs at the
     *     getRmsd call.
     *
     * The exception is caught rather than allowed to abort, because what the
     * census wants from these two jobs is a recording, and an aborted Spark job
     * writes its message to stderr with task and stage numbers in it. The
     * sentinel below names the fact without quoting the exception: the message
     * and the exception class are both things Spark 4.0 is entitled to change,
     * and an expectation recorded against them would be one no migration could
     * satisfy (rule 5 makes the baseline the oracle for behavior, not for the
     * framework's phrasing). The full stack still goes to stderr, where the
     * census's red/green screen reads it.
     */
    /**
     * MyVariantDataset over the three answers upstream actually gives.
     *
     * The scroll loop's second iteration (:169-171) is the whole reason eight
     * pages of fixtures exist, and a row count is the number that says all
     * eight were walked: seven full pages and a short one is 7708 rows, one
     * page would be 1000.
     *
     * The no-matches return at :115-117 is not reached by an accession with no
     * variants, which is what it looks like it is for. myvariant.info answers
     * such a query 200 with "hits":[], Spark infers array&lt;string&gt; for an
     * empty array, and flattenDataset's hits._id then fails to resolve -- so
     * getVariations throws where it means to return null (NOTES 2.5 #11). That
     * is recorded here rather than avoided, as a sentinel rather than as a
     * message: the AnalysisException's text is Spark's to change.
     *
     * What does reach :115-117 is a download that fails, because the dataset
     * then has no columns at all. An empty string in the id list concatenates
     * into "...dbnsfp.uniprot.acc:" with nothing after the colon, which
     * myvariant.info rejects with a 400, and the IOException out of openStream
     * is also the only route to getData's catch at :139-141.
     */
    private static void myvariant(String[] args) throws Exception {
        SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-myvariant").getOrCreate();

        String paged = args.length > 0 ? args[0] : "P00533";
        String nohits = args.length > 1 ? args[1] : "P0A7B8";

        Dataset<Row> none = MyVariantDataset.getVariations(Arrays.asList(""));
        System.out.println("myvariant <empty accession>: "
                + (none == null ? "no dataset" : "rows=" + none.count()));

        try {
            Dataset<Row> ds = MyVariantDataset.getVariations(
                    Arrays.asList(nohits));
            System.out.println("myvariant " + nohits + ": "
                    + (ds == null ? "no dataset" : "rows=" + ds.count()));
        } catch (Exception e) {
            // Spark's AnalysisException is a checked exception javac cannot
            // see: Scala declares no throws clause, so naming it in the catch
            // is rejected as never thrown. It is identified by type inside the
            // body instead, and anything else is rethrown -- a bare catch here
            // would swallow a genuine fault and print the sentinel for it.
            if (!(e instanceof AnalysisException)) {
                throw e;
            }
            System.out.println("myvariant " + nohits
                    + ": an empty hit list does not flatten on this tree");
        }

        Dataset<Row> ds = MyVariantDataset.getVariations(
                Arrays.asList(paged));
        System.out.println("myvariant " + paged + " rows: " + ds.count());
        System.out.println("myvariant " + paged + " distinct ids: "
                + ds.select("variationId").distinct().count());

        SparkSession.builder().getOrCreate().stop();
    }

    /**
     * DrugBankDataset's four authenticated entry points.
     *
     * Only getOpenDrugLinks has a caller in the tree, so half the file was
     * dead: the two enums, the three URL-building overloads, the authenticated
     * getDataset and the whole of getInputStream -- 25 of 52 lines, which is
     * why it was the one file below rule 7's bar that is not a deliberate RED.
     *
     * The credentials are nonsense on purpose. The mirror gates these four
     * names on the presence of an Authorization header and not on its
     * contents, because what upstream actually checks is not a thing this
     * workspace can reproduce and what the client can get wrong is whether it
     * sends one at all (NOTES 1.6.19).
     *
     * The three row counts differ so the three recordings cannot be confused.
     */
    private static void drugbank(String[] args) throws Exception {
        SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-drugbank").getOrCreate();

        String user = args.length > 0 ? args[0] : "mmtf-spark-census";
        String pass = args.length > 1 ? args[1] : "not-a-real-password";

        Dataset<Row> links = DrugBankDataset.getDrugLinks(
                DrugBankDataset.DrugGroup.APPROVED, user, pass);
        System.out.println("drugbank structure links: " + links.count()
                + " row(s), columns " + Arrays.toString(links.columns()));

        Dataset<Row> byGroup = DrugBankDataset.getDrugTargetLinks(
                DrugBankDataset.DrugGroup.APPROVED, user, pass);
        System.out.println("drugbank target links by group: " + byGroup.count()
                + " row(s), columns " + Arrays.toString(byGroup.columns()));

        Dataset<Row> byType = DrugBankDataset.getDrugTargetLinks(
                DrugBankDataset.DrugType.SMALL_MOLECULE, user, pass);
        System.out.println("drugbank target links by type: " + byType.count()
                + " row(s), columns " + Arrays.toString(byType.columns()));

        SparkSession.builder().getOrCreate().stop();
    }

    private static boolean materialize(String label, Dataset<Row> ds) {
        try {
            ds.show(20, false);
            return true;
        } catch (Exception e) {
            System.out.println(label + ": dataset does not materialize on this tree");
            e.printStackTrace();
            return false;
        }
    }


    /**
     * PdbToUniProt.readData, the local-filesystem twin of downloadData.
     *
     * PdbToUniProt is the file this migration disturbs most, and readData is 43
     * of its lines with no caller anywhere in the tree -- not a demo, not a
     * test. Its sibling downloadData is exercised by the SIFTS fixtures
     * (NOTES 1.6.9) and sits at 35/42; readData sat at 0/43, which is a block an
     * agent can delete for free while every test stays green.
     *
     * The path it wants is a local rsync of EBI's split_xml, which is exactly
     * the layout the mirror already carries: <path>/<xx>/<id>.xml.gz, with xx
     * the second and third characters of the lower-cased id. So the fixtures
     * serve this without a second copy -- the same bytes the FTP mirror hands
     * downloadData, read off the filesystem instead. The ids are spelled out
     * rather than globbed from the directory: a job whose input is whatever
     * happens to be in a directory changes its recording whenever the fixture
     * tree grows.
     */
    private static void siftslocal(String[] args) throws Exception {
        String path = args.length > 0 ? args[0]
                : "/srv/svc/ftp.ebi.ac.uk/sifts/split_xml";
        List<String> ids = args.length > 1
                ? Arrays.asList(Arrays.copyOfRange(args, 1, args.length))
                : Arrays.asList("1STP", "1STQ", "2ONX", "4MYA");

        SparkSession spark = SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-siftslocal").getOrCreate();
        JavaSparkContext sc = new JavaSparkContext(spark.sparkContext());

        StructType schema = new StructType().add("structureId",
                DataTypes.StringType, false);
        List<Row> rows = new ArrayList<>();
        for (String id : ids) {
            rows.add(RowFactory.create(id));
        }
        Dataset<Row> in = spark.createDataFrame(rows, schema);

        Dataset<Row> ds = PdbToUniProt.readData(in, path)
                .sort("structureChainId", "pdbSeqNum");
        System.out.println("siftslocal: " + ids + " from " + path);
        ds.printSchema();
        ds.show(20, false);
        System.out.println("Rows: " + ds.count());

        sc.close();
    }


    /**
     * MmtfImporter's SWISS-MODEL family: the three public entry points that
     * fetch homology models, and the five private methods underneath them.
     *
     * MmtfImporter is 78 of 172 lines covered on the baseline, the worst ratio
     * in io/, and nearly the whole hole is here. SwissModelDemo reaches
     * SwissModelDataset and stops there -- that class fetches the JSON metadata
     * and never a model -- so nothing in the tree ever asks for coordinates.
     * downloadSwissModelsByUniProtIds, downloadSwissModelsByUrls,
     * importSwissModelRepository, getFromPdbString, getFromPdbUrl,
     * getUniProtIdFromPath, getUniProtIdFromUrl, setSwissModelHeader and -- no
     * SWISS-MODEL model carries a SEQRES record -- the whole of
     * createSeqresRecords are dead on the baseline, and dead code is code an
     * agent can delete with every test still green.
     *
     * All three entry points run over the three accessions SwissModelDemo
     * names. The local-repository case wants the layout an rsync of the
     * SWISS-MODEL repository has,
     * &lt;root&gt;/&lt;ac[0:2]&gt;/&lt;ac[2:4]&gt;/&lt;ac[4:6]&gt;/swissmodel/,
     * because getUniProtIdFromPath reads the accession back out of the path by
     * character offsets from the literal "swissmodel". It is built here by
     * fetching the same three models through the mirror, so the image carries
     * one copy of those bytes and the request log sees every read.
     */
    private static void swissmodel(String[] args) throws Exception {
        String repo = args.length > 0 ? args[0] : "/tmp/prod/SWISS-MODEL_Repository";
        List<String> ids = args.length > 1
                ? Arrays.asList(Arrays.copyOfRange(args, 1, args.length))
                : Arrays.asList("O00244", "P24539", "P36575");

        // getUniProtIdFromPath takes the *first* "swissmodel" in the path, so a
        // root that contains the word would have it read nine characters out of
        // the wrong place and return junk accessions rather than fail. Refuse
        // that here instead of recording it.
        if (repo.contains("swissmodel")) {
            throw new IllegalArgumentException(
                    "repository root must not contain \"swissmodel\": " + repo);
        }

        SparkSession spark = SparkSession.builder().master("local[*]")
                .appName("HarnessDriver-swissmodel").getOrCreate();
        JavaSparkContext sc = new JavaSparkContext(spark.sparkContext());

        Path first = null;
        for (String ac : ids) {
            Path dir = Paths.get(repo, ac.substring(0, 2), ac.substring(2, 4),
                    ac.substring(4, 6), "swissmodel");
            Files.createDirectories(dir);
            Path model = dir.resolve(ac + ".pdb");
            try (InputStream is = new URL(SMR_URL + ac + SMR_PROVIDER).openStream()) {
                Files.copy(is, model, StandardCopyOption.REPLACE_EXISTING);
            }
            if (first == null) {
                first = model;
            }
        }

        summarize("swissmodel: byUniProtIds",
                MmtfImporter.downloadSwissModelsByUniProtIds(ids, sc));

        // The coordinate URLs carry range= and template=, and the several
        // models of one accession differ; sorted so the recording does not
        // depend on the order explode() happened to produce.
        List<String> urls = SwissModelDataset.getSwissModels(ids)
                .select("coordinates").as(Encoders.STRING()).collectAsList();
        urls = new ArrayList<>(urls);
        Collections.sort(urls);
        System.out.println("swissmodel: " + urls.size() + " coordinate url(s)");
        for (String url : urls) {
            System.out.println("  " + url);
        }
        summarize("swissmodel: byUrls",
                MmtfImporter.downloadSwissModelsByUrls(urls, sc));

        summarize("swissmodel: repository",
                MmtfImporter.importSwissModelRepository(repo, sc));

        String text = new String(Files.readAllBytes(first), StandardCharsets.UTF_8);
        System.out.println("swissmodel: fromString  "
                + describe(MmtfImporter.getFromPdbString(text, "SMR1")));

        sc.close();
    }

    /**
     * MmtfImporter.downloadPdbRedo, and through it getFromMmcifUrl.
     *
     * getFromMmcifUrl is 17 lines, none of them covered on the baseline:
     * PdbRedoToMmtf, the one application in the tree that names PDB-REDO, calls
     * importPdbRedo against a local directory instead and never touches the
     * download path.
     *
     * The id list mixes entries PDB-REDO has with one it does not, because half
     * of what this method does is fail: getFromMmcifUrl catches, prints
     * "WARNING: could not parse", and returns null for downloadPdbRedo's filter
     * to drop. A job that only ever succeeded would leave the catch deletable,
     * and the count below would not move if the filter went with it.
     */
    private static void pdbredo(String[] args) throws Exception {
        List<String> ids = args.length > 0 ? Arrays.asList(args)
                : Arrays.asList("1AIE", "1STP", "2ONX");

        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-pdbredo");
        JavaSparkContext sc = new JavaSparkContext(conf);

        System.out.println("pdbredo: requested " + ids);
        summarize("pdbredo: downloaded", MmtfImporter.downloadPdbRedo(ids, sc));

        sc.close();
    }

    /**
     * One sorted line per structure.
     *
     * Sorted because an RDD's order is its partition order, which is not the
     * property under test. The header fields are printed rather than a count
     * alone because setSwissModelHeader is the only thing that writes them: a
     * count passes whether or not it ran.
     */
    private static void summarize(String label,
            JavaPairRDD<String, StructureDataInterface> pdb) {
        List<String> lines = new ArrayList<>(
                pdb.map(t -> t._1 + "  " + describe(t._2)).collect());
        Collections.sort(lines);
        System.out.println(label + " n=" + lines.size());
        for (String line : lines) {
            System.out.println("  " + line);
        }
    }

    /**
     * Rule 7's numeric stratum, over the three filters that compare a double.
     *
     * Resolution, Rfree and Rwork are each two fields and one expression --
     * `v >= min && v <= max` -- with no guard in front of them, so every
     * interesting input is a value of min and max rather than a malformed
     * argument. The cases below are the boundary set rule 7 names, chosen for
     * what each one makes the *expression* do rather than for a round number:
     *
     *   NaN on either side   makes both comparisons false however large the
     *                        range, which is the one way this filter answers
     *                        "none" while looking like it answers "all";
     *   [-Inf, +Inf]         is the widest range expressible, and is NOT the
     *                        whole corpus: getResolution() answers NaN for the
     *                        NMR and EM entries, and NaN >= -Inf is false. The
     *                        gap between this answer and the corpus size is the
     *                        count of entries with no resolution at all;
     *   inverted bounds      are empty by construction, and the answer has to
     *                        be 0 rather than the whole corpus;
     *   [-0.0, 0.0]          is the signed-zero case: -0.0 == 0.0 in a double
     *                        comparison, so this is exactly the entries whose
     *                        resolution is 0.0, and the tree has some;
     *   Double.MIN_VALUE     is the smallest positive denormal, so [MIN_VALUE,
     *                        MAX_VALUE] is the corpus minus anything at exactly
     *                        zero, and differs from [0.0, MAX_VALUE] by that
     *                        count alone.
     *
     * Every case is counted, not shown, and the count is over the same 82-entry
     * reduced corpus every other filter job uses, so two cases that differ by
     * one entry differ by one in the recording.
     */
    private static void advnumeric(String[] args) throws Exception {
        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-advnumeric");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfReducedPath(), sc).cache();
        System.out.println("advnumeric: corpus=" + pdb.count());

        double inf = Double.POSITIVE_INFINITY;
        double nan = Double.NaN;

        counted("res widest", pdb.filter(new Resolution(-inf, inf)));
        counted("res nan min", pdb.filter(new Resolution(nan, inf)));
        counted("res nan max", pdb.filter(new Resolution(-inf, nan)));
        counted("res nan both", pdb.filter(new Resolution(nan, nan)));
        counted("res inverted", pdb.filter(new Resolution(inf, -inf)));
        counted("res signed zero", pdb.filter(new Resolution(-0.0, 0.0)));
        counted("res denormal min", pdb.filter(new Resolution(Double.MIN_VALUE, Double.MAX_VALUE)));
        counted("res zero min", pdb.filter(new Resolution(0.0, Double.MAX_VALUE)));
        counted("res point", pdb.filter(new Resolution(2.0, 2.0)));
        counted("res negative", pdb.filter(new Resolution(-1.0, -0.5)));

        counted("rfree widest", pdb.filter(new Rfree(-inf, inf)));
        counted("rfree nan both", pdb.filter(new Rfree(nan, nan)));
        counted("rfree inverted", pdb.filter(new Rfree(1.0, 0.0)));
        counted("rfree unit", pdb.filter(new Rfree(0.0, 1.0)));
        counted("rfree past one", pdb.filter(new Rfree(1.0, Double.MAX_VALUE)));

        counted("rwork widest", pdb.filter(new Rwork(-inf, inf)));
        counted("rwork nan both", pdb.filter(new Rwork(nan, nan)));
        counted("rwork inverted", pdb.filter(new Rwork(1.0, 0.0)));
        counted("rwork unit", pdb.filter(new Rwork(0.0, 1.0)));

        sc.close();
    }

    /**
     * Rule 7's text stratum, over the two filters that compare a String.
     *
     * DepositionDate and ReleaseDate hold two strings and run
     * `date.compareTo(start) >= 0 && date.compareTo(end) <= 0` -- a lexicographic
     * comparison on an ISO date, which is the same as a chronological one for
     * well-formed input and is not for anything else. There is no parse and no
     * guard, so a malformed bound is not an error here; it is a different
     * comparison, and which entries fall inside it is the recorded behavior.
     *
     *   ""                   sorts before every non-empty string, so it is a
     *                        lower bound that admits everything and an upper
     *                        bound that admits nothing;
     *   inverted             is empty by construction;
     *   "9999-99-99"         is not a date and is a perfectly good upper bound;
     *   a non-BMP character   sorts above every BMP one by UTF-16 code unit, so
     *                        it is an upper bound that admits the whole corpus,
     *                        and it is here because String.compareTo compares
     *                        code units and a migration that reached for a
     *                        collator would answer differently;
     *   a 4096-character bound tests nothing about length and is recorded
     *                        anyway, because a filter that truncated its
     *                        argument would answer differently and nothing else
     *                        in the suite would notice.
     */
    private static void advtext(String[] args) throws Exception {
        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-advtext");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfReducedPath(), sc).cache();
        System.out.println("advtext: corpus=" + pdb.count());

        // Written as escapes, not as the character. javac takes its source
        // encoding from the platform charset, which is ASCII on the JDK 8
        // rung and UTF-8 on the JDK 18+ rungs: as a literal character this
        // line compiled on the far end of the ladder and failed on the near
        // one. The escapes decode to the same two chars under every charset,
        // so the harness pins the same bound on every rung. It is a filter
        // bound and never reaches stdout, so no recording depends on how the
        // container would have encoded it on the way out either.
        String nonBmp = "\uD83D\uDCA9";     // U+1F4A9, a surrogate pair
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < 4096; i++) {
            sb.append('9');
        }
        String veryLong = sb.toString();

        counted("dep empty both", pdb.filter(new DepositionDate("", "")));
        counted("dep empty low", pdb.filter(new DepositionDate("", "9999")));
        counted("dep inverted", pdb.filter(new DepositionDate("9999", "0000")));
        counted("dep nondate high", pdb.filter(new DepositionDate("0000", "9999-99-99")));
        counted("dep nonbmp high", pdb.filter(new DepositionDate("", nonBmp)));
        counted("dep nonbmp low", pdb.filter(new DepositionDate(nonBmp, veryLong)));
        counted("dep very long high", pdb.filter(new DepositionDate("", veryLong)));
        counted("dep point", pdb.filter(new DepositionDate("2000-01-28", "2000-01-28")));

        counted("rel empty both", pdb.filter(new ReleaseDate("", "")));
        counted("rel empty low", pdb.filter(new ReleaseDate("", "9999")));
        counted("rel inverted", pdb.filter(new ReleaseDate("9999", "0000")));
        counted("rel nondate high", pdb.filter(new ReleaseDate("0000", "9999-99-99")));

        // ContainsSequenceRegex compiles its argument, so here the malformed
        // input IS an error and the message is what is recorded.
        counted("regex empty", pdb.filter(new ContainsSequenceRegex("")));
        counted("regex impossible", pdb.filter(new ContainsSequenceRegex("Z{40}")));
        counted("regex anchored all", pdb.filter(new ContainsSequenceRegex("^.*$")));
        expectKind("regex unbalanced", () -> new ContainsSequenceRegex("(").call(null));
        expectKind("regex bad quantifier", () -> new ContainsSequenceRegex("*").call(null));

        // ContainsGroup is varargs, so "no groups at all" is expressible and is
        // not the same question as "a group that does not exist".
        counted("group none", pdb.filter(new ContainsGroup()));
        counted("group empty name", pdb.filter(new ContainsGroup("")));
        counted("group absent", pdb.filter(new ContainsGroup("XXX")));
        counted("group present", pdb.filter(new ContainsGroup("ZN")));
        counted("group repeated", pdb.filter(new ContainsGroup("ZN", "ZN")));

        sc.close();
    }

    /**
     * Rule 7's interactions stratum: filters composed with each other.
     *
     * NotFilter and OrFilter take other filters, which is the only place in the
     * filter package where one class's answer is another's input. Each case
     * below is a composition whose answer is fixed by the algebra rather than
     * by the corpus -- Not(Not(f)) is f, Or(f, Not(f)) is everything, Or(f, f)
     * is f -- so a migration that changed either class's short-circuiting or
     * its null handling breaks an identity, not just a count.
     */
    private static void advcompose(String[] args) throws Exception {
        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-advcompose");
        JavaSparkContext sc = new JavaSparkContext(conf);

        JavaPairRDD<String, StructureDataInterface> pdb =
                MmtfReader.readSequenceFile(MmtfReader.getMmtfReducedPath(), sc).cache();
        System.out.println("advcompose: corpus=" + pdb.count());

        counted("base dna", pdb.filter(new ContainsDnaChain()));
        counted("not dna", pdb.filter(new NotFilter(new ContainsDnaChain())));
        counted("not not dna",
                pdb.filter(new NotFilter(new NotFilter(new ContainsDnaChain()))));
        counted("or dna self",
                pdb.filter(new OrFilter(new ContainsDnaChain(), new ContainsDnaChain())));
        counted("or dna notdna",
                pdb.filter(new OrFilter(new ContainsDnaChain(),
                        new NotFilter(new ContainsDnaChain()))));
        counted("not or dna notdna",
                pdb.filter(new NotFilter(new OrFilter(new ContainsDnaChain(),
                        new NotFilter(new ContainsDnaChain())))));
        counted("or rna dna",
                pdb.filter(new OrFilter(new ContainsRnaChain(), new ContainsDnaChain())));
        counted("or saccharide dprotein",
                pdb.filter(new OrFilter(new ContainsDSaccharideChain(),
                        new ContainsDProteinChain())));
        counted("base rna exclusive", pdb.filter(new ContainsRnaChain(true)));
        counted("base saccharide exclusive", pdb.filter(new ContainsDSaccharideChain(true)));
        counted("base lprotein exclusive", pdb.filter(new ContainsLProteinChain(true)));
        counted("or exclusive pair",
                pdb.filter(new OrFilter(new ContainsRnaChain(true),
                        new ContainsDnaChain(true))));
        // An empty range composed with a full one: the identity Or(empty, all)
        // == all has to hold whatever the corpus is.
        counted("or empty full",
                pdb.filter(new OrFilter(new Resolution(1.0, 0.0),
                        new Resolution(Double.NEGATIVE_INFINITY,
                                Double.POSITIVE_INFINITY))));
        counted("not empty",
                pdb.filter(new NotFilter(new Resolution(1.0, 0.0))));

        // Both classes dereference the filter they were handed, so a null is an
        // NPE at call time rather than at construction time. Which of the two it
        // is is the recorded behavior.
        expect("not null ctor", () -> new NotFilter(null));
        expect("or null ctor", () -> new OrFilter(null, null));

        sc.close();
    }

    /**
     * Rule 7's malformed-input stratum, over the reader.
     *
     * MmtfReader.readSequenceFile is the entry point every job in this suite
     * starts from, so what it does with a path that is missing, empty, a plain
     * file, or a Hadoop directory containing bytes that are not MMTF is the
     * error surface with the widest blast radius in the tree. None of it is
     * reached by any demo, all of it is reached by an agent who breaks the
     * reader, and each case is recorded as the exception class and the first
     * line of its message.
     */
    private static void advio(String[] args) throws Exception {
        SparkConf conf = new SparkConf().setMaster("local[*]")
                .setAppName("HarnessDriver-advio");
        JavaSparkContext sc = new JavaSparkContext(conf);

        Path tmp = Files.createTempDirectory("advio");
        Path missing = tmp.resolve("nothing-here");
        Path emptyDir = Files.createDirectories(tmp.resolve("empty"));
        Path plainFile = Files.write(tmp.resolve("plain.txt"),
                "not a sequence file\n".getBytes(StandardCharsets.UTF_8));
        Path junkDir = Files.createDirectories(tmp.resolve("junk"));
        Files.write(junkDir.resolve("part-00000"),
                new byte[] {0, 1, 2, 3, 4, 5, 6, 7});

        expectKind("read missing dir",
                () -> System.out.println("  read missing dir n=" + MmtfReader
                        .readSequenceFile(missing.toString(), sc).count()));
        expectKind("read empty dir",
                () -> System.out.println("  read empty dir n=" + MmtfReader
                        .readSequenceFile(emptyDir.toString(), sc).count()));
        expectKind("read plain file",
                () -> System.out.println("  read plain file n=" + MmtfReader
                        .readSequenceFile(plainFile.toString(), sc).count()));
        expectKind("read junk part",
                () -> System.out.println("  read junk part n=" + MmtfReader
                        .readSequenceFile(junkDir.toString(), sc).count()));

        // The id-filtered overload, whose second argument is a whitelist. An
        // empty list is a legal request for nothing; a list of ids that are not
        // in the corpus is the same request spelled differently; and an id with
        // the wrong case is the one people actually get wrong.
        String reduced = MmtfReader.getMmtfReducedPath();
        expectKind("read ids empty",
                () -> System.out.println("  read ids empty n=" + MmtfReader
                        .readSequenceFile(reduced, new ArrayList<String>(), sc).count()));
        expectKind("read ids absent",
                () -> System.out.println("  read ids absent n=" + MmtfReader
                        .readSequenceFile(reduced, Arrays.asList("ZZZZ"), sc).count()));
        expectKind("read ids lowercase",
                () -> System.out.println("  read ids lowercase n=" + MmtfReader
                        .readSequenceFile(reduced, Arrays.asList("1o06"), sc).count()));
        expectKind("read ids mixed",
                () -> System.out.println("  read ids mixed n=" + MmtfReader
                        .readSequenceFile(reduced, Arrays.asList("1O06", "ZZZZ"), sc).count()));
        expectKind("read ids duplicate",
                () -> System.out.println("  read ids duplicate n=" + MmtfReader
                        .readSequenceFile(reduced, Arrays.asList("1O06", "1O06"), sc).count()));
        expectKind("read ids empty string",
                () -> System.out.println("  read ids empty string n=" + MmtfReader
                        .readSequenceFile(reduced, Arrays.asList(""), sc).count()));

        // MmtfImporter's local readers, on the same shapes of bad input.
        //
        // These three are the whole local-import surface: importPdbFiles and
        // importMmcifFiles go through sc.wholeTextFiles, importPdbRedo through
        // a directory walk. There is no importMmtfFiles -- MMTF arrives through
        // MmtfReader above -- so the matrix is three readers over the two paths
        // that do not exist and the one that exists and is empty. Missing and
        // empty are separated on purpose: Hadoop's InputFormat rejects the
        // first at job-submission time and returns nothing for the second, and
        // a migration that changes which of those two a reader does is exactly
        // the kind of quiet change this census exists to catch.
        expectKind("import pdb missing",
                () -> System.out.println("  import pdb missing n=" + MmtfImporter
                        .importPdbFiles(missing.toString(), sc).count()));
        expectKind("import pdb empty",
                () -> System.out.println("  import pdb empty n=" + MmtfImporter
                        .importPdbFiles(emptyDir.toString(), sc).count()));
        expectKind("import mmcif missing",
                () -> System.out.println("  import mmcif missing n=" + MmtfImporter
                        .importMmcifFiles(missing.toString(), sc).count()));
        expectKind("import mmcif empty",
                () -> System.out.println("  import mmcif empty n=" + MmtfImporter
                        .importMmcifFiles(emptyDir.toString(), sc).count()));
        expectKind("import pdbredo missing",
                () -> System.out.println("  import pdbredo missing n=" + MmtfImporter
                        .importPdbRedo(missing.toString(), sc).count()));

        sc.close();
    }

    private static String describe(StructureDataInterface s) {
        return String.format(
                "chains=%d groups=%d atoms=%d models=%d method=%s released=%s title=%s",
                s.getNumChains(), s.getNumGroups(), s.getNumAtoms(),
                s.getNumModels(), join(s.getExperimentalMethods()),
                s.getReleaseDate(), s.getTitle());
    }

    private static String join(String[] values) {
        return values == null ? "null" : String.join(",", Arrays.asList(values));
    }

}
