package scoreguard;

import java.util.ArrayList;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Properties;
import java.util.Set;
import java.util.TreeMap;
import javax.inject.Named;
import javax.inject.Singleton;
import org.apache.maven.AbstractMavenLifecycleParticipant;
import org.apache.maven.execution.MavenSession;
import org.apache.maven.model.Build;
import org.apache.maven.model.Model;
import org.apache.maven.model.Plugin;
import org.apache.maven.model.PluginExecution;
import org.apache.maven.model.ReportPlugin;
import org.apache.maven.model.Reporting;
import org.apache.maven.project.MavenProject;
import org.codehaus.plexus.util.xml.Xpp3Dom;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * What the verifier's Maven sessions are, read off Maven's own effective
 * models; and the verifier's instrument, taking its parameters from the
 * verifier alone.
 *
 * Loaded with {@code -Dmaven.ext.class.path} into the verifier's instrument
 * runs (pom-scan.sh's dependency listing and its help:effective-pom reading).
 * It runs after the reactor is read
 * and before any execution plan exists, on the effective models: inheritance,
 * interpolation and profile activation already applied, so what it sees is
 * what the session will do.
 *
 * Every run: it reports, one line per project, the profiles active in that
 * project's whole lineage ({@code getInjectedProfileIds}, parents included),
 * so a caller can require that an instrument run activated exactly what the
 * scored build did; and one line per
 * maven-compiler-plugin execution with that execution's effective
 * configuration -- inheritance merged, so a module's own
 * {@code <compilerArgs>} that replaces the parent's list shows up as the list
 * the compiler is given.
 *
 * Instrument runs only ({@code score.guard.plugin} set): two scored scans run
 * a plugin over the candidate's reactor and give it its options as -D user
 * properties, and Maven reads a mojo parameter from the pom first --
 * plugin-level {@code <configuration>} (build plugins or pluginManagement,
 * profiles included), a {@code default-cli} execution, the plugin's own
 * {@code <dependencies>}, and project {@code <properties>} named like a
 * parameter's property all reach the instrument. For the plugin named, this
 * drops the configuration, the plugin dependencies and the {@code default-cli}
 * execution ({@code score.guard.executions=all}: every execution), in build
 * plugins, pluginManagement and reporting; and it removes from every
 * project's properties each name in {@code score.guard.props}, the goal's
 * parameter properties as enumerated from the pinned plugin's descriptor.
 *
 * It decides nothing. Its lines are how a caller tells that it ran and what
 * it saw; every report line is logged before the first mojo runs.
 */
@Named("score-instrument-guard")
@Singleton
public class InstrumentGuard extends AbstractMavenLifecycleParticipant {

    private static final Logger LOG = LoggerFactory.getLogger(InstrumentGuard.class);

    private static final String COMPILER = "org.apache.maven.plugins:maven-compiler-plugin";
    private static final String[] COMPILER_PARAMS = {
        "compilerArgument", "fork", "failOnWarning", "showWarnings", "compilerId",
        "skipMain", "skip"};
    private static final String[] COMPILER_PROPS = {
        "maven.compiler.failOnWarning", "maven.compiler.showWarnings",
        "maven.compiler.fork", "maven.compiler.compilerId", "maven.main.skip",
        "maven.test.skip"};

    private int configurations;
    private int dependencyLists;
    private int executions;
    private int declarations;

    @Override
    public void afterProjectsRead(MavenSession session) {
        Properties user = session.getUserProperties();
        int projects = 0;
        for (MavenProject project : session.getProjects()) {
            projects++;
            report(project);
        }
        LOG.info("[score-guard] report: {} project(s)", projects);

        String key = user.getProperty("score.guard.plugin", "").trim();
        if (key.isEmpty()) {
            return;
        }
        boolean all = "all".equals(user.getProperty("score.guard.executions", "cli").trim());
        Set<String> props = new LinkedHashSet<>();
        for (String p : user.getProperty("score.guard.props", "").split(",")) {
            if (!p.trim().isEmpty()) {
                props.add(p.trim());
            }
        }
        int removed = 0;
        for (MavenProject project : session.getProjects()) {
            Model model = project.getModel();
            Build build = model.getBuild();
            if (build != null) {
                for (Plugin plugin : build.getPlugins()) {
                    neutralise(plugin, key, all);
                }
                if (build.getPluginManagement() != null) {
                    for (Plugin plugin : build.getPluginManagement().getPlugins()) {
                        neutralise(plugin, key, all);
                    }
                }
            }
            Reporting reporting = model.getReporting();
            if (reporting != null) {
                for (ReportPlugin plugin : reporting.getPlugins()) {
                    if (key.equals(keyOf(plugin.getGroupId(), plugin.getArtifactId()))) {
                        declarations++;
                        if (plugin.getConfiguration() != null) {
                            configurations++;
                        }
                        plugin.setConfiguration(null);
                        plugin.setReportSets(new ArrayList<>());
                    }
                }
            }
            Properties own = model.getProperties();
            if (own != null) {
                for (String p : props) {
                    if (own.remove(p) != null) {
                        removed++;
                    }
                }
            }
        }
        LOG.info("[score-guard] {}: {} project(s); {} declaration(s) found, {} configuration(s), "
                + "{} dependency list(s), {} execution(s) dropped; {} project propert(ies) removed "
                + "of {} named", key, projects, declarations, configurations, dependencyLists,
                executions, removed, props.size());
    }

    /** One `profiles` line per project, one `javac` line per compiler execution. */
    private void report(MavenProject project) {
        String id = project.getGroupId() + ":" + project.getArtifactId();
        Map<String, List<String>> lineage = new TreeMap<>();
        Map<String, List<String>> injected = project.getInjectedProfileIds();
        if (injected != null) {
            for (Map.Entry<String, List<String>> e : injected.entrySet()) {
                List<String> ids = new ArrayList<>(e.getValue() == null
                        ? new ArrayList<String>() : e.getValue());
                java.util.Collections.sort(ids);
                lineage.put(String.valueOf(e.getKey()), ids);
            }
        }
        StringBuilder p = new StringBuilder();
        p.append("{\"project\":").append(q(id)).append(",\"profiles\":{");
        boolean first = true;
        for (Map.Entry<String, List<String>> e : lineage.entrySet()) {
            if (!first) {
                p.append(',');
            }
            first = false;
            p.append(q(e.getKey())).append(':').append(list(e.getValue()));
        }
        p.append("}}");
        LOG.info("[score-guard] profiles {}", p);

        Build build = project.getModel().getBuild();
        if (build == null) {
            return;
        }
        Properties props = project.getProperties();
        for (Plugin plugin : build.getPlugins()) {
            if (!COMPILER.equals(keyOf(plugin.getGroupId(), plugin.getArtifactId()))) {
                continue;
            }
            for (PluginExecution e : plugin.getExecutions()) {
                StringBuilder j = new StringBuilder();
                j.append("{\"project\":").append(q(id))
                 .append(",\"packaging\":").append(q(project.getPackaging()))
                 .append(",\"execution\":").append(q(e.getId()))
                 .append(",\"phase\":").append(q(e.getPhase()))
                 .append(",\"goals\":").append(list(e.getGoals()));
                Xpp3Dom cfg = e.getConfiguration() instanceof Xpp3Dom
                        ? (Xpp3Dom) e.getConfiguration() : null;
                List<String> args = new ArrayList<>();
                boolean hasArgs = false;
                if (cfg != null && cfg.getChild("compilerArgs") != null) {
                    hasArgs = true;
                    for (Xpp3Dom a : cfg.getChild("compilerArgs").getChildren()) {
                        args.add(a.getValue() == null ? "" : a.getValue());
                    }
                }
                j.append(",\"compilerArgs\":").append(hasArgs ? list(args) : "null");
                for (String name : COMPILER_PARAMS) {
                    Xpp3Dom c = cfg == null ? null : cfg.getChild(name);
                    j.append(",\"").append(name).append("\":")
                     .append(c == null ? "null" : q(c.getValue() == null ? "" : c.getValue()));
                }
                j.append(",\"properties\":{");
                boolean f = true;
                for (String name : COMPILER_PROPS) {
                    if (!f) {
                        j.append(',');
                    }
                    f = false;
                    String v = props == null ? null : props.getProperty(name);
                    j.append(q(name)).append(':').append(v == null ? "null" : q(v));
                }
                j.append("}}");
                LOG.info("[score-guard] javac {}", j);
            }
        }
    }

    private void neutralise(Plugin plugin, String key, boolean all) {
        if (!key.equals(keyOf(plugin.getGroupId(), plugin.getArtifactId()))) {
            return;
        }
        declarations++;
        if (plugin.getConfiguration() != null) {
            configurations++;
            plugin.setConfiguration(null);
        }
        if (plugin.getDependencies() != null && !plugin.getDependencies().isEmpty()) {
            dependencyLists++;
            plugin.setDependencies(new ArrayList<>());
        }
        List<PluginExecution> keep = new ArrayList<>();
        for (PluginExecution e : plugin.getExecutions()) {
            if (all || "default-cli".equals(e.getId())) {
                executions++;
            } else {
                keep.add(e);
            }
        }
        plugin.setExecutions(keep);
        plugin.flushExecutionMap();
    }

    private static String keyOf(String groupId, String artifactId) {
        String g = (groupId == null || groupId.trim().isEmpty())
                ? "org.apache.maven.plugins" : groupId.trim();
        return g + ":" + (artifactId == null ? "" : artifactId.trim());
    }

    private static String list(List<String> values) {
        StringBuilder s = new StringBuilder("[");
        if (values != null) {
            for (int i = 0; i < values.size(); i++) {
                if (i > 0) {
                    s.append(',');
                }
                s.append(q(values.get(i)));
            }
        }
        return s.append(']').toString();
    }

    /** A JSON string; every character outside printable ASCII is \\u-escaped. */
    private static String q(String v) {
        if (v == null) {
            return "null";
        }
        StringBuilder s = new StringBuilder("\"");
        for (int i = 0; i < v.length(); i++) {
            char c = v.charAt(i);
            if (c == '"' || c == '\\') {
                s.append('\\').append(c);
            } else if (c < 0x20 || c > 0x7e) {
                s.append(String.format("\\u%04x", (int) c));
            } else {
                s.append(c);
            }
        }
        return s.append('"').toString();
    }
}
