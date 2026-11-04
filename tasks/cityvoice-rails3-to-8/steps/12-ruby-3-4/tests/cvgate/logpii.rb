# What the application wrote into its own production log while it was driven.
#
#   ruby /tests/cvgate/logpii.rb --logs /logs/verifier/cvh --out gates/logpii.json
#
# This reads an artifact of the behaviour run rather than the source tree, and
# that is the point.  `config/environments/production.rb` sends the logger to
# `STDOUT`; `cvh/runner.py` gives each world's server process a file for its
# stdout and its stderr; so when phase 3 ends, `<log-dir>/<world>-server.log`
# is every line this application logged while the recorded scenarios were being
# driven through it.  A grep over `config/initializers/` would be a gate on a
# spelling -- `filter_parameters` can be written five ways, a log subscriber
# can redact instead, and `Rails.application.config.filter_parameters` can be
# assigned in three files -- and it would say nothing about what actually
# reached the disk.  This counts what reached the disk.
#
# Two readings, and the second is what stops the first being bought cheaply:
#
#   * How many lines carry a telephone number or an e-mail address.  On the
#     tree step 12 hands over: 132 lines with a number (`From` and `To` on
#     every Twilio callback) and 10 with an address (`subscription[email]`),
#     across six of the ten worlds, every one of them inside a `Parameters:`
#     line -- measured.
#   * How many lines the log has at all: `Started`, `Parameters:` and
#     `Completed`.  A tree that sets `config.log_level = :warn`, or points the
#     logger at `/dev/null`, has no numbers in its log and has not fixed
#     anything.  The counts are the floor that says so, and the step's profile
#     guards them.
#
# `runner.log` is deliberately NOT read: it is the harness's own log, written
# by the instrument rather than by the application, and a gate that read it
# would be scoring this package's own choice of fixtures.  Only
# `*-server.log`.
#
# The two patterns are written to match what an ACCESS LOG can plausibly carry
# rather than to validate an address: E.164 is a leading `+` and 10 to 15
# digits, and the address pattern is the usual local@domain.tld.  Both were
# checked against the ten recorded logs for false positives -- the only
# matches on the arriving tree are the fixtures' own numbers and addresses,
# and there are no matches at all outside `Parameters:` lines.
#
# Both also match a PARTLY MASKED value, and that is deliberate.  A tree that
# rewrites `+15105551234` to `+1510555****` before writing it has not stopped
# recording the caller -- seven digits of a North American number and the whole
# of its area code identify a neighbourhood, and an IVR's caller pool is a
# neighbourhood.  Without the second alternative, a `gsub` in a log formatter
# would score the same as `config.filter_parameters`, which is the difference
# between removing the value and decorating it.  The mask alternative wants two
# or more of `*`, `x`, `X` or `#` after an optional run of digits, so the 306
# `+0000` timezone offsets in the recorded logs match neither alternative and
# neither does anything else in them: measured 0 phone and 0 e-mail matches on
# the golden's ten logs, and 132 and 10 on the arriving tree's.  The e-mail
# local part accepts the same mask characters for the same reason
# (`r***@example.com` is still the subscriber), and the domain does not,
# because the domain is the part a mask is never applied to and widening it
# would start matching file paths.
require 'json'
require 'optparse'

module CVGate
  module LogPII
    # A telephone number as this application's callers present one: E.164 with
    # a leading `+`.  Three forms have to count and one must not.
    #
    #   +15105550101         written out, which is what the provider sends
    #   +1 510 555 0101      reformatted, with any of the separators a human or
    #   +1-510-555-0101      a formatter puts between the groups
    #   +1510555****         partially masked
    #   +0000                a UTC offset, which is NOT a telephone number
    #
    # Two stages rather than one pattern.  PHONE_RUN over-matches -- `+`
    # followed by a run of digits and separators -- and `phone?` decides by
    # counting the DIGITS in the run, because what makes `+15105550101` a
    # telephone number and `+0000` a UTC offset is how many digits it has and
    # not what sits between them.  Written as a single regex the separator
    # class makes the length bound meaningless: `\+[\d\s().\-]{10,15}` matches
    # `+0000 2016-01-1` out of a log timestamp.  Written in two stages the bound
    # is on the digits, where it belongs.
    #
    # The reformatted forms matter because a log subscriber that REFORMATS
    # rather than redacts leaves the number in the log in clear text.
    # Measured against the 2834 lines the scored drive writes on both the
    # golden and the floor tree: the reformatted forms add no false matches
    # there -- the same 132 lines match, and nothing else does.
    PHONE_RUN = /\+[\d\s().\-]{8,20}\d/.freeze
    PHONE_MASKED = /\+\d{0,12}[*xX#]{2,}/.freeze
    PHONE_MIN_DIGITS = 10
    PHONE_MAX_DIGITS = 15
    EMAIL = /[A-Za-z0-9._%+\-*#]+@[A-Za-z0-9.\-]+\.[A-Za-z]{2,}/.freeze
    STARTED = /^\s*Started [A-Z]+ "/.freeze
    PARAMETERS = /^\s*Parameters:/.freeze
    COMPLETED = /^\s*Completed \d/.freeze
    # Enough of a match to identify it in a report, and not enough to make the
    # evidence file a second copy of the leak.  The log itself is collected
    # whole, so nothing is being hidden here; this is about the size and the
    # readability of `evidence.json`.
    SAMPLE_LIMIT = 5

    def self.scan(paths)
      lines = 0
      started = 0
      parameters = 0
      completed = 0
      filtered = 0
      found = { 'phone' => Hash.new(0), 'email' => Hash.new(0) }
      outside = { 'phone' => 0, 'email' => 0 }
      hits = { 'phone' => 0, 'email' => 0 }
      per_file = {}

      paths.each do |path|
        file_hits = { 'phone' => 0, 'email' => 0 }
        File.foreach(path, encoding: 'UTF-8') do |raw|
          line = raw.scrub('?')
          lines += 1
          started += 1 if line =~ STARTED
          in_parameters = (line =~ PARAMETERS) ? true : false
          parameters += 1 if in_parameters
          completed += 1 if line =~ COMPLETED
          filtered += line.scan('[FILTERED]').length
          { 'phone' => nil, 'email' => EMAIL }.each do |kind, pattern|
            matches = pattern ? line.scan(pattern) : phones(line)
            next if matches.empty?

            hits[kind] += 1
            file_hits[kind] += 1
            outside[kind] += 1 unless in_parameters
            matches.each { |m| found[kind][m[0, 64]] += 1 }
          end
        end
        per_file[File.basename(path)] = file_hits
      end

      { 'lines' => lines, 'started_lines' => started,
        'parameter_lines' => parameters, 'completed_lines' => completed,
        'filtered_markers' => filtered, 'per_file' => per_file,
        'phone' => summarise(found['phone'], hits['phone'], outside['phone']),
        'email' => summarise(found['email'], hits['email'], outside['email']) }
    end

    # The masked branch is kept separate from the digit count on purpose: a
    # partially masked number has fewer digits left than a whole one, so a
    # digit-count test would reject exactly the form the mask is hiding.
    def self.phones(line)
      masked = line.scan(PHONE_MASKED)
      runs = line.scan(PHONE_RUN).select do |run|
        n = run.count('0-9')
        next false unless n >= PHONE_MIN_DIGITS && n <= PHONE_MAX_DIGITS
        # `+1510555012****` is one finding, not two: the digits in front of a
        # mask can be long enough to pass the count on their own.
        masked.none? { |m| m.start_with?(run) }
      end
      runs + masked
    end

    def self.summarise(counts, lines, outside)
      { 'lines' => lines, 'distinct' => counts.length,
        'outside_parameters' => outside,
        'samples' => counts.keys.sort.first(SAMPLE_LIMIT) }
    end

    def self.report(dir)
      unless File.directory?(dir)
        return { 'dir' => dir, 'error' => "no log directory at #{dir}" }
      end

      paths = Dir.glob(File.join(dir, '*-server.log')).sort
      if paths.empty?
        # Not an `error`: the directory is there and the run produced nothing
        # in it, which is a reading about the tree -- it served no world -- and
        # the floor the profile guards is what fails on it.  An `error` here
        # would fail the gates through `score.py`'s failed-probe path instead,
        # which reports the instrument rather than the tree.
        return { 'dir' => dir, 'files' => [], 'bytes' => 0, 'lines' => 0,
                 'started_lines' => 0, 'parameter_lines' => 0,
                 'completed_lines' => 0, 'filtered_markers' => 0,
                 'per_file' => {},
                 'phone' => summarise({}, 0, 0),
                 'email' => summarise({}, 0, 0) }
      end

      scan(paths).merge(
        'dir' => dir,
        'files' => paths.map { |p| File.basename(p) },
        # `inject` rather than `sum`: `Array#sum` arrived in Ruby 2.4 and this
        # file runs under the interpreter each STEP targets, which is 2.3.8 on
        # rung 01.  scan.rb's `rule_lex` makes the same choice about `match?`
        # for the same reason.  Only running this script under 2.3.8 shows it
        # -- `ruby -c` is happy with a NoMethodError.
        'bytes' => paths.inject(0) { |acc, p| acc + File.size(p) }
      )
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { logs: '/logs/verifier/cvh' }
  OptionParser.new do |opts|
    opts.on('--logs PATH') { |v| options[:logs] = v }
    opts.on('--out FILE') { |v| options[:out] = v }
  end.parse!(ARGV)

  payload = JSON.pretty_generate(CVGate::LogPII.report(options[:logs]))
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
end
