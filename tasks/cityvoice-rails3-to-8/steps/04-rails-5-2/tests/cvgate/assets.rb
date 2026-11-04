# The asset-pipeline backstop: Sprockets manifest directives.
#
#   ruby -I/gates /gates/cvgate/assets.rb --root /app
#
# Grep is a backstop only, and this is the backstop.  The primary measurement
# of "Sprockets is gone" is the resolved dependency graph
# (`gemgraph.rb`) and the booted application's own configuration
# (`bootprobe.rb`): a tree with no `sprockets` gem and no `config.assets` cannot
# be running Sprockets whatever its files say.  What that pair cannot see is a
# *leftover*: a `//= require_tree .` sitting in `application.js` after the gem
# is gone, which is dead text pretending to be a build instruction.
#
# The directive format is Sprockets' own: a comment on one of the first lines of
# an asset, opened by `//=`, `#=` or ` *=`.  Sprockets parses it with a regular
# expression too, and only in the leading comment block -- a `//= require` in
# the middle of a file is not a directive to Sprockets and is not one here.

require 'json'
require 'optparse'

module CVGate
  module Assets
    ASSET_SUFFIXES = %w[.js .css .scss .sass .coffee .erb .less].freeze

    DIRECTIVE = /\A\s*(?:\/\/|\#|\*)=\s*(\w+)(?:\s+(.*?))?\s*\z/.freeze

    # A directive block ends at the first line that is not a comment and not
    # blank -- the same rule Sprockets applies.
    COMMENT_LINE = /\A\s*(?:\/\/|\#|\*|\/\*|\*\/)/.freeze

    def self.asset_files(root)
      root = File.expand_path(root)
      roots = [File.join(root, 'app/assets'), File.join(root, 'lib/assets'),
               File.join(root, 'vendor/assets'), File.join(root, 'app/javascript')]
      roots.flat_map do |dir|
        next [] unless File.directory?(dir)

        Dir.glob(File.join(dir, '**', '*'), File::FNM_DOTMATCH).select do |path|
          File.file?(path) && path.end_with?(*ASSET_SUFFIXES)
        end
      end.uniq.sort
    end

    def self.scan(root)
      root = File.expand_path(root)
      findings = []
      files = asset_files(root)
      files.each do |path|
        File.read(path, encoding: 'UTF-8').each_line.with_index(1) do |line, number|
          stripped = line.chomp
          break unless stripped.strip.empty? || COMMENT_LINE.match(stripped)

          match = DIRECTIVE.match(stripped)
          next unless match

          findings << {
            'rule' => 'sprockets_directive',
            'file' => path.sub(/\A#{Regexp.escape(root)}\/?/, ''),
            'line' => number,
            'detail' => "Sprockets manifest directive: #{match[1]} #{match[2]}".strip
          }
        end
      end
      { 'root' => root, 'assets_scanned' => files.length,
        'counts' => { 'sprockets_directive' => findings.length },
        'total' => findings.length, 'findings' => findings }
    end
  end
end

if $PROGRAM_NAME == __FILE__
  options = { root: '/app' }
  OptionParser.new do |opts|
    opts.on('--root ROOT') { |v| options[:root] = v }
    opts.on('--out FILE') { |v| options[:out] = v }
  end.parse!(ARGV)

  payload = JSON.pretty_generate(CVGate::Assets.scan(options[:root]))
  if options[:out]
    File.write(options[:out], payload + "\n")
  else
    puts payload
  end
end
