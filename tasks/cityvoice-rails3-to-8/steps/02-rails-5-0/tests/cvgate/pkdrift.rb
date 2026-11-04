# frozen_string_literal: true

# The two constructions of this application's schema, read separately and
# compared.
#
#   # one reading of whatever database is live right now
#   cd /app && bundle exec ruby /gates/cvgate/pkdrift.rb \
#       --app /app --read --out /tmp/gates/pk-dump.json
#
#   # the comparison, over two such readings
#   ruby /gates/cvgate/pkdrift.rb --compare \
#       --dump /tmp/gates/pk-dump.json --migrated /tmp/gates/pk-migrated.json \
#       --load-exit 0 --migrate-exit 0 --out /tmp/gates/pkdrift.json
#
# `db/schema.rb` and `db/migrate/` are two descriptions of the same database
# and a tree can move one without the other.  The failure this exists to catch
# is constructible: keep every migration declared `[4.2]`, write `id: :bigint`
# on the `create_table`s, and hand-edit the dump back to the narrow key.  An
# instrument that reads either side alone lets that side answer for both.
#
# TWO READINGS AND NOT ONE, AND WHY THE FIRST IS TAKEN FROM A SAVED FILE
#
# The dump side cannot be read from the database the rest of the verifier has
# been using.  `rake db:migrate` invokes `db:_dump` when it finishes, and
# `ActiveRecord::Base.dump_schema_after_migration` defaults to **true** on
# every release this ladder spans -- the generated `config/environments/
# production.rb` is what turns it off, and this application's production file
# never had that line.  The behaviour harness runs `db:migrate` as one of its
# recorded cases.  So by the time phase 4 starts, `db/schema.rb` in the
# candidate's tree is not the file the candidate handed on: it is whatever this
# framework dumps, which on 5.1 is the pinned, `id: :serial` form the step is
# asking for.  A gate that read the database built from *that* file would be
# scoring the verifier's own repair work -- a tree that never touched its dump
# would pass, which is the exact tree the gate exists to fail.
#
# So `test.sh` copies `db/schema.rb` in phase 1, before anything runs, puts
# that copy back before building the dump side, and puts it back again at the
# end so the tree handed to the next step is the candidate's own.
#
# WHAT `pinned` IS A CONJUNCTION OF
#
#   agree         same tables, and each table's key the same width on both
#                 sides.  Width and not spelling: PostgreSQL's catalogue has
#                 called a 4-byte key `integer`, `int4` and `serial` over the
#                 adapters this ladder spans, and "the same 32-bit key" is the
#                 requirement, not "the same string".  A spelling difference at
#                 equal width is recorded for the log and is not a finding.
#   both_narrow   neither side has a 64-bit key, and neither has a width this
#                 probe does not recognise.  An unrecognised width is a finding
#                 rather than a pass: an instrument that classifies what it
#                 knows and stays silent about the rest reports clean on the
#                 case it was built for.
#   wide_default  the framework's own default for a table that does not name a
#                 key is the 64-bit one.  This conjunct is what stops the
#                 comparison from being a free point on a release that still
#                 defaults narrow: there the two sides agree however careless
#                 the tree is, and the handed-on 5.0 tree is exactly that
#                 release.
#   exits         a `schema:load` or a `db:migrate` that failed is not a
#                 reading of anything.
#
# Every conjunct is in the report beside the verdict, so a failure names which
# half went wrong rather than leaving `false` to be interpreted.

require 'json'
require 'optparse'

require_relative 'bootprobe'

module CVGate
  module PkDrift
    INTERNAL_TABLES = %w[schema_migrations ar_internal_metadata].freeze

    # One place where a SQL type becomes a width, and it is `BootProbe`'s list
    # rather than a second copy of it.  Two lists of spellings that drift apart
    # would turn into a disagreement this probe reported as the tree's.
    def self.width_of(sql_type)
      type = sql_type.to_s
      return 'narrow' if CVGate::BootProbe::NARROW_KEY_TYPES.include?(type)
      return 'wide' if CVGate::BootProbe::WIDE_KEY_TYPES.include?(type)
      'unrecognised'
    end

    def self.classify(id_types)
      widths = CVGate::BootProbe.primary_key_widths('', id_types)
      { 'tables' => id_types.keys.sort,
        'id_sql_types' => id_types,
        'id_widths' => Hash[id_types.keys.sort.map { |t| [t, width_of(id_types[t])] }],
        'narrow_id_tables' => widths['narrow_id_tables'],
        'wide_id_tables' => widths['wide_id_tables'],
        'odd_id_tables' => widths['odd_id_tables'] }
    end

    # One side, as `--read` wrote it.  Every way of not having a reading is a
    # named problem and never an empty one: a missing file, an unparseable one
    # and an application that did not boot would all otherwise produce "no
    # tables on this side", which compares equal to the same thing on the other
    # side and reads as a pass.
    def self.read_side(path, side)
      return [nil, nil, "no #{side} reading was named"] unless path
      begin
        reading = JSON.parse(File.read(path))
      rescue StandardError => e
        return [nil, nil, "the #{side} reading (#{path}) could not be read: " \
                          "#{e.class}: #{e.message}"]
      end
      unless reading.is_a?(Hash)
        return [nil, nil, "the #{side} reading (#{path}) is not an object"]
      end
      unless reading['booted']
        return [nil, reading, "the application did not boot for the #{side} " \
                              "reading: #{reading['boot_error'] || 'no reason recorded'}"]
      end
      id_types = reading['id_sql_types']
      unless id_types.is_a?(Hash)
        return [nil, reading, "the #{side} reading recorded no id_sql_types: " \
                              "#{reading['read_error'] || 'absent'}"]
      end
      [id_types, reading, nil]
    end

    # The live connection.  `lease_connection` for the same reason `bootprobe`
    # uses it: on Rails 7.2 and later `connection` works and deprecates, and an
    # instrument that raises the warning it is measured beside manufactures its
    # own finding.
    def self.connection
      if ActiveRecord::Base.respond_to?(:lease_connection)
        ActiveRecord::Base.lease_connection
      else
        ActiveRecord::Base.connection
      end
    end

    def self.read_live
      connection = self.connection
      default_key = begin
        connection.native_database_types[:primary_key].to_s
      rescue StandardError
        nil
      end
      id_types = {}
      (connection.tables.to_a - INTERNAL_TABLES).sort.each do |table|
        column = begin
          connection.columns(table).find { |c| c.name == 'id' }
        rescue StandardError
          nil
        end
        id_types[table] = column.sql_type.to_s if column
      end
      applied = begin
        if connection.table_exists?('schema_migrations')
          connection.select_values('SELECT count(*) FROM schema_migrations').first.to_i
        end
      rescue StandardError
        nil
      end
      [default_key, id_types, applied]
    end

    # `--read`: boot the application and write down what is in the database
    # right now.  Nothing here compares anything; the comparison is a separate
    # process over two of these files, so that the database can be destroyed
    # and rebuilt between them by the candidate's own rake tasks.
    def self.read(app)
      report = { 'app' => app, 'ruby' => RUBY_VERSION }
      begin
        require File.join(app, 'config', 'environment')
        report['booted'] = true
      rescue Exception => e # rubocop:disable Lint/RescueException
        report['booted'] = false
        report['boot_error'] = "#{e.class}: #{e.message}"
        report['boot_backtrace'] = Array(e.backtrace).first(20)
        return report
      end
      begin
        default_key, id_types, applied = read_live
        report['default_primary_key'] = default_key
        report['id_sql_types'] = id_types
        report['migrations_applied'] = applied
      rescue Exception => e # rubocop:disable Lint/RescueException
        report['read_error'] = "#{e.class}: #{e.message}"
      end
      report
    end

    # The whole of the verdict, as a pure function, so that `selftest_tools.rb`
    # can put a disagreeing pair through it without a database.  A comparison
    # that has only ever been run against trees that agree is a comparison
    # nobody has checked: it would answer "pinned" on the tree it was written
    # for and "pinned" on the tree it was written against, and those two look
    # the same in the reward.
    def self.compare(dump_types, live_types, default_key,
                     load_exit, migrate_exit, problems = [])
      problems = problems.dup
      dump = dump_types ? classify(dump_types) : nil
      live = live_types ? classify(live_types) : nil
      wide_default = default_key.to_s.include?('bigserial') ||
                     default_key.to_s.include?('bigint')

      only_dump = []
      only_live = []
      differing = {}
      spellings = {}
      if dump && live
        only_dump = (dump['tables'] - live['tables']).sort
        only_live = (live['tables'] - dump['tables']).sort
        (dump['tables'] & live['tables']).each do |table|
          pair = { 'dump' => dump_types[table], 'migrated' => live_types[table],
                   'dump_width' => dump['id_widths'][table],
                   'migrated_width' => live['id_widths'][table] }
          if pair['dump_width'] == pair['migrated_width']
            # The same key, spelled two ways by the catalogue.  Recorded so a
            # log reader can see it happened, and deliberately not a finding:
            # the requirement is the same 32-bit key, not the same string.
            spellings[table] = pair unless dump_types[table] == live_types[table]
          else
            differing[table] = pair
          end
        end
        problems << "the two databases name different tables" unless
          only_dump.empty? && only_live.empty?
        problems << "#{differing.length} table(s) have a different key width in " \
                    'the two databases' unless differing.empty?
        problems << "neither database has a table with an id column" if dump['tables'].empty?
      end
      problems << "no reading from the dump side" if dump.nil?
      problems << "no reading from the migrated side" if live.nil?
      problems << "db:schema:load exited #{load_exit}" unless load_exit.to_i.zero?
      problems << "db:migrate exited #{migrate_exit}" unless migrate_exit.to_i.zero?
      wide_or_odd = []
      [['dump', dump], ['migrated', live]].each do |side, reading|
        next unless reading
        wide_or_odd += reading['wide_id_tables'].map { |t| "#{side}:#{t} is a 64-bit key" }
        wide_or_odd += reading['odd_id_tables'].map do |t|
          "#{side}:#{t} has a key width this probe does not recognise " \
            "(#{reading['id_sql_types'][t]})"
        end
      end
      problems += wide_or_odd

      agree = !dump.nil? && !live.nil? && only_dump.empty? && only_live.empty? &&
              differing.empty? && !dump['tables'].empty?
      both_narrow = !dump.nil? && !live.nil? && wide_or_odd.empty?

      { 'dump' => dump,
        'migrated' => live,
        'default_primary_key' => default_key,
        'wide_default_primary_key' => wide_default,
        'schema_load_exit' => load_exit.to_i,
        'migrate_exit' => migrate_exit.to_i,
        'tables_only_in_dump' => only_dump,
        'tables_only_in_migrated' => only_live,
        'differing_tables' => differing,
        'differing_spellings' => spellings,
        'agree' => agree,
        'both_narrow' => both_narrow,
        'pinned' => agree && both_narrow && wide_default &&
                    load_exit.to_i.zero? && migrate_exit.to_i.zero?,
        'problems' => problems }
    end

    # `--compare`: two reading files in, one verdict out.  No application is
    # booted here, so nothing this process does can disturb the tree.
    def self.run(dump_path, migrated_path, load_exit, migrate_exit)
      dump_types, dump_reading, dump_problem = read_side(dump_path, 'dump')
      live_types, live_reading, live_problem = read_side(migrated_path, 'migrated')
      problems = [dump_problem, live_problem].compact

      default_key = (live_reading && live_reading['default_primary_key']) ||
                    (dump_reading && dump_reading['default_primary_key'])
      report = { 'dump_source' => dump_path, 'migrated_source' => migrated_path,
                 'ruby' => RUBY_VERSION }
      %w[booted migrations_applied].each do |key|
        report[key] = live_reading[key] if live_reading && live_reading.key?(key)
      end
      report.merge(compare(dump_types, live_types, default_key,
                           load_exit, migrate_exit, problems))
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { app: '/app', mode: nil, dump: nil, migrated: nil, out: nil,
              load_exit: 0, migrate_exit: 0 }
  OptionParser.new do |opts|
    opts.on('--app PATH') { |v| options[:app] = v }
    opts.on('--read') { options[:mode] = :read }
    opts.on('--compare') { options[:mode] = :compare }
    opts.on('--dump FILE') { |v| options[:dump] = v }
    opts.on('--migrated FILE') { |v| options[:migrated] = v }
    opts.on('--load-exit N') { |v| options[:load_exit] = v.to_i }
    opts.on('--migrate-exit N') { |v| options[:migrate_exit] = v.to_i }
    opts.on('--out FILE') { |v| options[:out] = v }
  end.parse!(ARGV)

  result =
    if options[:mode] == :read
      app = File.expand_path(options[:app])
      Dir.chdir(app)
      CVGate::PkDrift.read(app)
    else
      CVGate::PkDrift.run(options[:dump], options[:migrated],
                          options[:load_exit], options[:migrate_exit])
    end

  payload = JSON.pretty_generate(result)
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
  # Always zero, for the reason `depdrive.rb` gives: the finding is the file,
  # and a non-zero exit would make a tree whose two databases disagree
  # indistinguishable from a probe that could not run.
  exit 0
end
