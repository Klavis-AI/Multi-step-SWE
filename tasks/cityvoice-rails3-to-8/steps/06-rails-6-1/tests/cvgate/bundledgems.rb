# frozen_string_literal: true

# Which libraries this process loaded out of the interpreter's own lib
# directory, of the ones the interpreter has announced it is going to stop
# shipping.
#
# Ruby has been moving libraries out of the default set for four releases:
# each one is a "bundled gem", still installed with the interpreter and still
# requirable, and each one stops being installed with the interpreter at a
# named release.  Rubygems says so at require time, once per library:
#
#   .../ostruct.rb was loaded from the standard library, but will no longer be
#   part of the default gems starting from Ruby 4.0.0.  Add ostruct to your
#   Gemfile or gemspec.
#
# THAT SENTENCE IS NOT WHAT THIS FILE READS, and the reason is the whole point
# of it.  The sentence goes through `Kernel#warn`, so it is not printed when
# `$VERBOSE` is nil -- which is what `RUBYOPT=-W0` leaves behind, and what any
# line in the application can leave behind -- and it is not seen by a collector
# that some other `Warning.singleton_class.prepend` sits in front of.  A check
# written against the list of sentences is a check a tree can empty without
# touching what it loads: measured, the collector reads zero lines on a tree
# that is still loading stdlib `ostruct` under three different ways of turning
# the channel off.
#
# What this file reads instead is the load path itself, after the boot:
#
#   * `$LOADED_FEATURES`, for files under the interpreter's own `rubylibdir`
#     (which is where a bundled gem lives while it is still a default gem, and
#     which is NOT where an installed gem lives),
#   * whose name is one the interpreter itself lists in
#     `Gem::BUNDLED_GEMS::SINCE`, so the list moves with the interpreter rather
#     than being a list this ladder maintains,
#   * and which `Gem.loaded_specs` does not have, which is how a tree that
#     declared the gem and got the default copy activated anyway is told apart
#     from a tree that declared nothing.
#
# Both fixes a candidate can make clear it, which is the outcome the step asks
# for and not a particular edit: declaring the gem moves the file out of
# `rubylibdir` (or puts the name in `loaded_specs`), and not requiring the
# library at all leaves it out of `$LOADED_FEATURES`. Measured both ways on two
# trees.
#
# `$VERBOSE`, `Warning` and `RUBYOPT` cannot move any of the three readings.
#
# The selection is a pure function of four arrays so that the selftest can
# drive it on every interpreter in the ladder, including the five that have no
# `Gem::BUNDLED_GEMS` at all and answer the empty list for every tree.
module CVGate
  module BundledGems
    # The interpreter's own list, or nil where the interpreter has none.  Nil
    # rather than `[]`: "this release moves no libraries" and "this release has
    # not been asked" are the same answer for a gate and different answers for
    # a reader, and `known?` is what the report carries.
    def self.names
      return nil unless defined?(Gem::BUNDLED_GEMS)
      since = Gem::BUNDLED_GEMS.const_get(:SINCE)
      return nil unless since.respond_to?(:keys)

      since.keys.map { |name| name.to_s }
    rescue Exception # rubocop:disable Lint/RescueException
      # A rubygems that has the constant under another shape is a reading this
      # probe does not have, not a crash of the gate that asked for it.
      nil
    end

    def self.known?
      !names.nil?
    end

    # features  -- `$LOADED_FEATURES`, absolute paths
    # libdir    -- `RbConfig::CONFIG['rubylibdir']`
    # arch      -- `RbConfig::CONFIG['arch']`, so that a C extension sitting in
    #              `rubylibdir/<arch>/` is read under its library name.  A
    #              bundled gem can be a C extension: `bigdecimal` and `fiddle`
    #              are, and both are on 3.4's list.
    # names     -- the interpreter's list, or nil
    # activated -- `Gem.loaded_specs.keys`
    #
    # A library whose name has a dash is a directory on disk -- `net-smtp` is
    # `net/smtp.rb` -- so the comparison is against the path under `rubylibdir`
    # with the separators put back, not against the basename.  `racc` is on the
    # list and `racc/parser.rb` is not `racc`, so the comparison is exact.
    def self.select(features, libdir, arch, names, activated)
      return [] if names.nil?

      prefix = libdir.to_s.sub(%r{/+\z}, '')
      return [] if prefix.empty?

      prefix += '/'
      archdir = arch.to_s.empty? ? nil : arch.to_s + '/'
      wanted = {}
      names.each { |name| wanted[name.to_s.tr('-', '/')] = name.to_s }
      seen = {}
      Array(activated).each { |name| seen[name.to_s] = true }

      found = []
      Array(features).each do |feature|
        path = feature.to_s
        next unless path.start_with?(prefix)

        rel = path[prefix.length, path.length].to_s.sub(/\.(rb|so|bundle|dll|o|sl)\z/, '')
        rel = rel[archdir.length, rel.length].to_s if archdir && rel.start_with?(archdir)
        name = wanted[rel]
        next if name.nil? || seen[name]

        found << name
      end
      found.uniq.sort
    end

    # The same selection, asked of this process.  Wrapped, because a probe that
    # crashes reading the load path costs the gates that had nothing to do with
    # it -- `bootprobe.rb`'s house rule.
    def self.read
      activated = begin
        Gem.loaded_specs.keys
      rescue Exception # rubocop:disable Lint/RescueException
        []
      end
      select($LOADED_FEATURES, RbConfig::CONFIG['rubylibdir'],
             RbConfig::CONFIG['arch'], names, activated)
    rescue Exception => e # rubocop:disable Lint/RescueException
      { 'error' => "#{e.class}: #{e.message}" }
    end
  end
end
