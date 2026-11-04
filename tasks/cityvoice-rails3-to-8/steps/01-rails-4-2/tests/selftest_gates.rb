# Rehearse every construct rule against a known answer before any of them is
# used to score anything.
#
# Every rule gets both halves:
#
#   * a POSITIVE fixture that must produce exactly N findings, and
#   * a NEGATIVE fixture -- the near miss that the rule must NOT fire on: the
#     same word in a comment, in a string, as a symbol, as a local variable, as
#     a method definition, or in the already-migrated spelling.
#
# A rule that only ever sees its positive fixture is a rule that might be
# matching the whole world.  `no_findings` is therefore not a formality; it is
# half the test.
#
#   ruby -I. selftest_gates.rb

require 'fileutils'
require 'tmpdir'
require 'json'
require_relative 'cvgate/scan'

$checks = 0
$failures = []
$names = {}
$positive = {}   # rule -> a fixture that must produce findings was run
$negative = {}   # rule -> a near miss that must produce none was run

MIN_CHECKS = 173

def check(name)
  raise "duplicate check name: #{name}" if $names[name]

  $names[name] = true
  $checks += 1
  begin
    yield
  rescue StandardError => e
    $failures << "#{name}: raised #{e.class}: #{e.message}"
  end
end

def fail!(name, message)
  $failures << "#{name}: #{message}"
end

# Scan one in-memory source as though it were a file in a repo.
def scan(source, rules, filename: 'app/models/thing.rb')
  Dir.mktmpdir do |dir|
    path = File.join(dir, filename)
    FileUtils.mkdir_p(File.dirname(path))
    File.write(path, source)
    CVGate::Scanner.new(dir, Array(rules)).run.to_h
  end
end

def expect_count(name, source, rule, want, filename: 'app/models/thing.rb')
  (want.positive? ? $positive : $negative)[rule] = true
  check(name) do
    got = scan(source, rule, filename: filename)
    n = got['counts'].fetch(rule, 0)
    if n != want
      fail!(name, "want #{want} #{rule} findings, got #{n}: " \
                  "#{got['findings'].map { |f| "#{f['line']}:#{f['detail']}" }.join(' | ')}")
    end
    if got['parse_errors'].any?
      fail!(name, "unexpected parse errors: #{got['parse_errors'].inspect}")
    end
  end
end

# ---------------------------------------------------------------------------
# scan.annotations -- the schema comment an annotating gem writes above a model
#
# Not a rule: a reading of the tree, so these get `check` and a helper of their
# own rather than `expect_count`.  The fixture is the real block from this
# repository's `app/models/answer.rb`, byte for byte, which is why its digest
# can be asserted against a measured constant -- and why that assertion is also
# a check that SHA256 over the same twelve lines gives the same twelve
# characters on every interpreter in the ladder, from 2.3 to 3.4.
# ---------------------------------------------------------------------------

def want_ann(name, got, expected)
  fail!(name, "want #{expected.inspect}, got #{got.inspect}") unless got == expected
end

# Scan a whole tree rather than one file: the reading is a property of
# `app/models` as a directory, and the sum and the digest can only be wrong
# once there is more than one model in it.
def scan_annotations(files)
  Dir.mktmpdir do |dir|
    files.each do |name, source|
      path = File.join(dir, name)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, source)
    end
    CVGate::Scanner.new(dir, []).run.to_h['annotations']
  end
end

ANNOTATED = <<~RUBY
  # == Schema Information
  #
  # Table name: answers
  #
  #  id                 :integer          not null, primary key
  #  question_id        :integer
  #  voice_file_url     :string(255)
  #  numerical_response :integer
  #  created_at         :datetime
  #  updated_at         :datetime
  #  call_id            :integer
  #

  class Answer < ApplicationRecord
    belongs_to :call
  end
RUBY

check 'annotations: the real block is twelve lines and seven column rows' do
  got = scan_annotations('app/models/answer.rb' => ANNOTATED)
  one = got['models']['app/models/answer.rb']
  want_ann('annotations: count', got['count'], 1)
  want_ann('annotations: columns', got['columns'], 7)
  want_ann('annotations: lines', got['lines'], 12)
  want_ann('annotations: per-model columns', one['columns'], 7)
  # Measured on the repository's own file, on ruby 2.1.7 and on 3.2.11.
  want_ann('annotations: digest', one['digest'], '66e298d2f867')
end

check 'annotations: a model with no block is no model here' do
  got = scan_annotations('app/models/plain.rb' => "class Plain < ApplicationRecord\nend\n")
  want_ann('annotations: no block, count', got['count'], 0)
  want_ann('annotations: no block, columns', got['columns'], 0)
end

# THE failure this reading exists to catch: the gem is dropped, something
# rewrites the head of the file, and what is left says `Schema Information` and
# lists nothing.  A digest would catch it -- and so does a column count, which
# additionally survives a legitimate re-generation that a digest does not.
check 'annotations: a stripped block is still a model but has no columns' do
  got = scan_annotations('app/models/answer.rb' => <<~RUBY)
    # == Schema Information
    #
    # Table name: answers
    #

    class Answer < ApplicationRecord
    end
  RUBY
  want_ann('annotations: stripped, count', got['count'], 1)
  want_ann('annotations: stripped, columns', got['columns'], 0)
  want_ann('annotations: stripped, lines', got['lines'], 4)
end

check 'annotations: prose rows are not column rows' do
  got = scan_annotations('app/models/answer.rb' => <<~RUBY)
    # == Schema Information
    #
    # Table name: answers
    # Indexes on this table are listed below
    #
    #  id :integer
    #

    class Answer; end
  RUBY
  want_ann('annotations: prose, columns', got['columns'], 1)
end

check 'annotations: the block stops where the code starts' do
  got = scan_annotations('app/models/answer.rb' => <<~RUBY)
    # == Schema Information
    #
    #  id :integer
    class Answer
      #  smuggled :integer
      def a; end
    end
  RUBY
  want_ann('annotations: stops, columns', got['columns'], 1)
  want_ann('annotations: stops, lines', got['lines'], 3)
end

check 'annotations: two models are summed, and the digest covers both paths' do
  both = { 'app/models/answer.rb' => ANNOTATED,
           'app/models/survey.rb' => ANNOTATED,
           'app/models/plain.rb' => "class Plain; end\n" }
  got = scan_annotations(both)
  want_ann('annotations: two, count', got['count'], 2)
  want_ann('annotations: two, columns', got['columns'], 14)
  # Same block at two paths: the tree digest must be neither model's own, and
  # must move when a block changes rather than when an unannotated file appears
  # beside it.
  one = scan_annotations('app/models/answer.rb' => ANNOTATED)
  if got['digest'] == one['digest']
    fail!('annotations: two, digest', 'two models digested as one')
  end
  shorter = ANNOTATED.sub("#  call_id            :integer\n", "")
  moved = scan_annotations(both.merge('app/models/survey.rb' => shorter))
  if moved['digest'] == got['digest']
    fail!('annotations: two, digest', 'a changed block did not move the digest')
  end
  want_ann('annotations: two, changed columns', moved['columns'], 13)
end

# ---------------------------------------------------------------------------
# attr_accessible
# ---------------------------------------------------------------------------

expect_count 'attr_accessible: two calls counted separately', <<~RUBY, 'attr_accessible', 2
  class Thing < ActiveRecord::Base
    attr_accessible :name, :body
    attr_protected :secret
  end
RUBY

expect_count 'attr_accessible: consolidation does not reduce the count', <<~RUBY, 'attr_accessible', 3
  class A < ActiveRecord::Base
    attr_accessible :a
    attr_accessible :b
    attr_accessible :c
  end
RUBY

expect_count 'attr_accessible: not in a comment', <<~RUBY, 'attr_accessible', 0
  class Thing < ActiveRecord::Base
    # attr_accessible :name was removed in favour of strong parameters
    def permitted
      params.require(:thing).permit(:name)
    end
  end
RUBY

expect_count 'attr_accessible: not in a string or a heredoc', <<~RUBY, 'attr_accessible', 0
  class Thing
    DOC = "attr_accessible :name"
    NOTE = <<~TEXT
      attr_accessible :name
      attr_protected :secret
    TEXT
  end
RUBY

expect_count 'attr_accessible: not as a symbol', <<~RUBY, 'attr_accessible', 0
  class Thing
    REMOVED = [:attr_accessible, :attr_protected]
  end
RUBY

expect_count 'attr_accessible: not as a method definition', <<~RUBY, 'attr_accessible', 0
  module Shim
    def attr_accessible(*names)
      names
    end
  end
RUBY

expect_count 'attr_accessible: the migrated spelling is clean', <<~RUBY, 'attr_accessible', 0
  class ThingsController < ApplicationController
    def thing_params
      params.require(:thing).permit(:name, :body)
    end
  end
RUBY

# ---------------------------------------------------------------------------
# before_filter
# ---------------------------------------------------------------------------

expect_count 'before_filter: each filter spelling counted', <<~RUBY, 'before_filter', 4
  class ApplicationController < ActionController::Base
    before_filter :authenticate
    after_filter :log
    around_filter :instrument
    skip_before_filter :authenticate, only: [:index]
  end
RUBY

expect_count 'before_filter: the 5.0 spelling is clean', <<~RUBY, 'before_filter', 0
  class ApplicationController < ActionController::Base
    before_action :authenticate
    after_action :log
    around_action :instrument
    skip_before_action :authenticate, only: [:index]
  end
RUBY

expect_count 'before_filter: `before_action :before_filter` is one call not two', <<~RUBY, 'before_filter', 0
  class C < ActionController::Base
    before_action :before_filter
  end
RUBY

# ---------------------------------------------------------------------------
# unversioned migration superclass
# ---------------------------------------------------------------------------

expect_count 'migration: bare ActiveRecord::Migration is a finding', <<~RUBY,
  class CreateCalls < ActiveRecord::Migration
    def change
      create_table :calls
    end
  end
RUBY
             'unversioned_migration', 1, filename: 'db/migrate/20140401145954_create_calls.rb'

expect_count 'migration: the versioned form is clean', <<~RUBY,
  class CreateCalls < ActiveRecord::Migration[7.1]
    def change
      create_table :calls
    end
  end
RUBY
             'unversioned_migration', 0, filename: 'db/migrate/20140401145954_create_calls.rb'

expect_count 'migration: an intermediate version is also clean', <<~RUBY,
  class CreateCalls < ActiveRecord::Migration[5.0]
    def change; end
  end
RUBY
             'unversioned_migration', 0, filename: 'db/migrate/1_x.rb'

expect_count 'migration: a subclass of a versioned base is clean', <<~RUBY,
  class Base < ActiveRecord::Migration[7.1]; end
  class CreateCalls < Base
    def change; end
  end
RUBY
             'unversioned_migration', 0, filename: 'db/migrate/1_x.rb'

expect_count 'migration: mentioning the constant without inheriting is clean', <<~RUBY,
  class Helper
    KIND = ActiveRecord::Migration
  end
RUBY
             'unversioned_migration', 0, filename: 'lib/helper.rb'

# ---------------------------------------------------------------------------
# misversioned migration superclass
#
# The complement of the rule above.  `unversioned_migration` asks whether the
# suffix is there; this asks whether it is the right one, because
# `ActiveRecord::Migration[5.0]` satisfies the first rule while changing what
# `create_table` and `add_reference` actually do.
# ---------------------------------------------------------------------------

expect_count 'misversioned: the version this tree keeps is clean', <<~RUBY,
  class CreateCalls < ActiveRecord::Migration[4.2]
    def change
      create_table :calls
    end
  end
RUBY
             'misversioned_migration', 0,
             filename: 'db/migrate/20140401145954_create_calls.rb'

expect_count 'misversioned: stamping a migration forward is a finding', <<~RUBY,
  class CreateCalls < ActiveRecord::Migration[5.0]
    def change
      create_table :calls
    end
  end
RUBY
             'misversioned_migration', 1,
             filename: 'db/migrate/20140401145954_create_calls.rb'

expect_count 'misversioned: a later release is a finding too', <<~RUBY,
  class CreateCalls < ActiveRecord::Migration[7.1]
    def change; end
  end
RUBY
             'misversioned_migration', 1, filename: 'db/migrate/1_x.rb'

# The bare form is the *other* rule's finding.  If it were both, a tree that
# never versioned anything would be charged twice for one mistake, and the
# profile that gates the two counts separately would report a defect it does
# not have.
expect_count 'misversioned: the bare form belongs to the other rule', <<~RUBY,
  class CreateCalls < ActiveRecord::Migration
    def change; end
  end
RUBY
             'misversioned_migration', 0, filename: 'db/migrate/1_x.rb'

expect_count 'misversioned: an index that is not a literal is reported', <<~RUBY,
  V = 4.2
  class CreateCalls < ActiveRecord::Migration[V]
    def change; end
  end
RUBY
             'misversioned_migration', 1, filename: 'db/migrate/1_x.rb'

expect_count 'misversioned: a subclass of a versioned base is clean', <<~RUBY,
  class Base < ActiveRecord::Migration[4.2]; end
  class CreateCalls < Base
    def change; end
  end
RUBY
             'misversioned_migration', 0, filename: 'db/migrate/1_x.rb'

expect_count 'misversioned: another constant indexed the same way is clean', <<~RUBY,
  class Thing < Some::Other[5.0]
    def change; end
  end
RUBY
             'misversioned_migration', 0, filename: 'lib/helper.rb'

# `scan.migrations`: the positive reading of the same property.
#
# The two rules above are counts of findings, and the second of them is zero on
# a tree that has versioned nothing -- a bare `ActiveRecord::Migration` is the
# first rule's finding and never the second's, by the fixture four above this
# one.  So `misversioned_migration == 0` is satisfied by the 43 unversioned
# migrations this step is handed, and a gate on it is a free point.  This is
# the reading that is not: what the migrations actually declare.

def migrations_of(files)
  Dir.mktmpdir do |dir|
    files.each do |rel, source|
      path = File.join(dir, rel)
      FileUtils.mkdir_p(File.dirname(path))
      File.write(path, source)
    end
    CVGate::Scanner.new(dir, ['unversioned_migration', 'misversioned_migration'])
                   .run.to_h
  end
end

# The six fixtures below are about the histogram over `db/migrate/` and pass no
# manifest, so they compare the three keys that reading owns.  The `shipped_*`
# keys have their own fixtures further down; here they are only asserted to be
# the no-manifest shape, which is nil and empty rather than zero.
def directory_histogram(got, name)
  shipped = got['migrations'].select { |k, _| k.start_with?('shipped') }
  fail!(name, "shipped keys without a manifest: #{shipped.inspect}") unless
    shipped == { 'shipped_total' => nil, 'shipped_unversioned' => nil,
                 'shipped_versions' => {} }
  got['migrations'].reject { |k, _| k.start_with?('shipped') }
end

check 'migrations: an unversioned tree declares nothing and misversions nothing' do
  # The free point, stated as a fixture: the two cells a reader would compare.
  name = 'migrations: an unversioned tree declares nothing and misversions nothing'
  got = migrations_of('db/migrate/1_a.rb' => "class A < ActiveRecord::Migration\n  def change; end\nend\n",
                      'db/migrate/2_b.rb' => "class B < ActiveRecord::Migration\n  def change; end\nend\n")
  want = { 'total' => 2, 'unversioned' => 2, 'versions' => {} }
  fail!(name, got['migrations'].inspect) unless directory_histogram(got, name) == want
  fail!(name, "misversioned=#{got['counts']['misversioned_migration']}") unless
    got['counts']['misversioned_migration'] == 0
end

check 'migrations: the declared version is read and counted' do
  name = 'migrations: the declared version is read and counted'
  got = migrations_of('db/migrate/1_a.rb' => "class A < ActiveRecord::Migration[4.2]\n  def change; end\nend\n",
                      'db/migrate/2_b.rb' => "class B < ActiveRecord::Migration[4.2]\n  def change; end\nend\n")
  want = { 'total' => 2, 'unversioned' => 0, 'versions' => { '4_2' => 2 } }
  fail!(name, got['migrations'].inspect) unless directory_histogram(got, name) == want
end

check 'migrations: a forward-stamped migration does not count as 4.2' do
  name = 'migrations: a forward-stamped migration does not count as 4.2'
  got = migrations_of('db/migrate/1_a.rb' => "class A < ActiveRecord::Migration[5.0]\n  def change; end\nend\n",
                      'db/migrate/2_b.rb' => "class B < ActiveRecord::Migration[4.2]\n  def change; end\nend\n")
  want = { 'total' => 2, 'unversioned' => 0,
           'versions' => { '4_2' => 1, '5_0' => 1 } }
  fail!(name, got['migrations'].inspect) unless directory_histogram(got, name) == want
end

check 'migrations: a suffix that is not a literal is its own bucket' do
  # Neither 4.2 nor a number: a version this scanner has not read.  It must not
  # land in the 4.2 bucket, and it must not be silently dropped either, or a
  # tree could reach the count with files nobody can read the version of.
  name = 'migrations: a suffix that is not a literal is its own bucket'
  got = migrations_of('db/migrate/1_a.rb' => "V = 4.2\nclass A < ActiveRecord::Migration[V]\n  def change; end\nend\n")
  want = { 'total' => 1, 'unversioned' => 0,
           'versions' => { 'not_a_literal' => 1 } }
  fail!(name, got['migrations'].inspect) unless directory_histogram(got, name) == want
end

check 'migrations: only db/migrate is read' do
  name = 'migrations: only db/migrate is read'
  got = migrations_of('lib/helper.rb' => "class A < ActiveRecord::Migration[4.2]\n  def change; end\nend\n")
  want = { 'total' => 0, 'unversioned' => 0, 'versions' => {} }
  fail!(name, got['migrations'].inspect) unless directory_histogram(got, name) == want
end

check 'migrations: a file with no migration class in it still counts as one file' do
  # `total` is files under db/migrate, not classes: a candidate who empties a
  # migration has a file that declares nothing, and "43 declare 4.2" has to be
  # the thing that fails, not `total`.
  name = 'migrations: a file with no migration class in it still counts as one file'
  got = migrations_of('db/migrate/1_a.rb' => "# nothing here\n")
  want = { 'total' => 1, 'unversioned' => 1, 'versions' => {} }
  fail!(name, got['migrations'].inspect) unless directory_histogram(got, name) == want
end


check 'misversioned: the two migration rules report separately' do
  got = scan(<<~RUBY, %w[unversioned_migration misversioned_migration],
    class A < ActiveRecord::Migration[5.0]; end
    class B < ActiveRecord::Migration; end
    class C < ActiveRecord::Migration[4.2]; end
  RUBY
             filename: 'db/migrate/1_x.rb')
  want = { 'unversioned_migration' => 1, 'misversioned_migration' => 1 }
  unless got['counts'] == want
    fail!('misversioned: the two migration rules report separately',
          "want #{want.inspect}, got #{got['counts'].inspect}")
  end
end

# ---------------------------------------------------------------------------
# render text: / nothing:
# ---------------------------------------------------------------------------

expect_count 'render: both hash spellings counted', <<~RUBY, 'render_text', 2
  class C < ActionController::Base
    def a
      render text: 'ok'
    end

    def b
      render :nothing => true, :status => 204
    end
  end
RUBY

expect_count 'render: parenthesised form counted', <<~RUBY, 'render_text', 1
  class C < ActionController::Base
    def a
      render(text: 'ok')
    end
  end
RUBY

expect_count 'render: `plain:` is clean', <<~RUBY, 'render_text', 0
  class C < ActionController::Base
    def a
      render plain: 'ok'
    end

    def b
      head :no_content
    end
  end
RUBY

expect_count 'render: a `text:` key on another method is clean', <<~RUBY, 'render_text', 0
  class C
    def a
      Notifier.build(text: 'ok')
    end
  end
RUBY

# ---------------------------------------------------------------------------
# redirect_to :back
# ---------------------------------------------------------------------------

expect_count 'redirect_to: :back is a finding', <<~RUBY, 'redirect_to_back', 1
  class C < ActionController::Base
    def a
      redirect_to :back
    end
  end
RUBY

expect_count 'redirect_to: the 5.0 replacement is clean', <<~RUBY, 'redirect_to_back', 0
  class C < ActionController::Base
    def a
      redirect_back fallback_location: root_path
    end
  end
RUBY

expect_count 'redirect_to: another symbol is clean', <<~RUBY, 'redirect_to_back', 0
  class C < ActionController::Base
    def a
      redirect_to :index
    end
  end
RUBY

# ---------------------------------------------------------------------------
# scope without a lambda
# ---------------------------------------------------------------------------

expect_count 'scope: eager relation body is a finding', <<~RUBY, 'scope_without_lambda', 1
  class Thing < ActiveRecord::Base
    scope :recent, where('created_at > ?', 1.week.ago)
  end
RUBY

expect_count 'scope: stabby lambda is clean', <<~RUBY, 'scope_without_lambda', 0
  class Thing < ActiveRecord::Base
    scope :recent, -> { where('created_at > ?', 1.week.ago) }
  end
RUBY

expect_count 'scope: `lambda {}` is clean', <<~RUBY, 'scope_without_lambda', 0
  class Thing < ActiveRecord::Base
    scope :recent, lambda { where(x: 1) }
  end
RUBY

expect_count 'scope: `Proc.new {}` is clean', <<~RUBY, 'scope_without_lambda', 0
  class Thing < ActiveRecord::Base
    scope :recent, Proc.new { where(x: 1) }
  end
RUBY

expect_count 'scope: a bodyless `scope :x` is not judged', <<~RUBY, 'scope_without_lambda', 0
  class Thing
    scope :recent
  end
RUBY

# ---------------------------------------------------------------------------
# removed Ruby constants and APIs
# ---------------------------------------------------------------------------

expect_count 'fixnum: each removed constant counted', <<~RUBY, 'fixnum', 3
  class Thing
    def a(x)
      x.is_a?(Fixnum) || x.is_a?(Bignum) || x == NIL
    end
  end
RUBY

expect_count 'fixnum: Integer is clean', <<~RUBY, 'fixnum', 0
  class Thing
    def a(x)
      x.is_a?(Integer)
    end
  end
RUBY

expect_count 'fixnum: a constant whose name merely contains it is clean', <<~RUBY, 'fixnum', 0
  class Thing
    FixnumHelper = 1
    MAX_FIXNUM = 2
  end
RUBY

expect_count 'uri_escape: URI.escape is a finding', <<~RUBY, 'uri_escape', 1
  class Thing
    def a(s)
      URI.escape(s)
    end
  end
RUBY

expect_count 'uri_escape: a bare local `escape` is clean', <<~RUBY, 'uri_escape', 0
  class Thing
    def a(s)
      escape(s)
    end

    def escape(s)
      s
    end
  end
RUBY

expect_count 'uri_escape: CGI.escape is clean', <<~RUBY, 'uri_escape', 0
  class Thing
    def a(s)
      CGI.escape(s)
    end
  end
RUBY

expect_count 'exists_predicate: File.exists? is a finding', <<~RUBY, 'exists_predicate', 1
  class Thing
    def a(p)
      File.exists?(p)
    end
  end
RUBY

expect_count 'exists_predicate: File.exist? is clean', <<~RUBY, 'exists_predicate', 0
  class Thing
    def a(p)
      File.exist?(p)
    end
  end
RUBY

expect_count 'exists_predicate: ActiveRecord `exists?` is clean', <<~RUBY, 'exists_predicate', 0
  class Thing
    def a
      Location.exists?(name: 'x')
    end
  end
RUBY

expect_count 'taint: the taint API is counted', <<~RUBY, 'taint', 2
  class Thing
    def a(s)
      s.taint
      s.tainted?
    end
  end
RUBY

expect_count 'safe_global: $SAFE is a finding', <<~RUBY, 'safe_global', 1
  class Thing
    def a
      $SAFE = 1
    end
  end
RUBY

expect_count 'safe_global: another global is clean', <<~RUBY, 'safe_global', 0
  class Thing
    def a
      $stdout.puts 'x'
    end
  end
RUBY

expect_count 'random_default: Random::DEFAULT is a finding', <<~RUBY, 'random_default', 1
  class Thing
    def a
      Random::DEFAULT.rand(10)
    end
  end
RUBY

expect_count 'random_default: Random.new is clean', <<~RUBY, 'random_default', 0
  class Thing
    def a
      Random.new.rand(10)
    end
  end
RUBY

expect_count 'alias_method_chain: counted', <<~RUBY, 'alias_method_chain', 1
  class Thing
    alias_method_chain :save, :logging
  end
RUBY

expect_count 'alias_method_chain: a plain `alias_method` is clean', <<~RUBY, 'alias_method_chain', 0
  class Thing
    alias_method :save_without_logging, :save
  end
RUBY

expect_count 'taint: `Module#prepend` migration of the same code is clean', <<~RUBY, 'taint', 0
  class Thing
    def a(s)
      s.frozen?
    end
  end
RUBY

expect_count 'update_attributes: counted, bang form too', <<~RUBY, 'update_attributes', 2
  class Thing
    def a(r)
      r.update_attributes(name: 'x')
      r.update_attributes!(name: 'y')
    end
  end
RUBY

expect_count 'update_attributes: `update` is clean', <<~RUBY, 'update_attributes', 0
  class Thing
    def a(r)
      r.update(name: 'x')
      r.update!(name: 'y')
    end
  end
RUBY

# strict_loading -- the marker half of 6.1's preloading requirement.  Counted
# only under app/, and only in the relation-returning form: the requirement is
# that the relations these controllers build carry the marker, and an
# unconfined count would be satisfiable by three lines in a spec file or three
# `strict_loading!` calls on an already-loaded record.  The preloading itself
# is read as a query count by `depdrive`; this rule is the marker only.
expect_count 'strict_loading: on a relation under app/, counted', <<~RUBY,
  class ThingsController
    def index
      @things = Thing.strict_loading.includes(:other)
    end
  end
RUBY
             'strict_loading', 1, filename: 'app/controllers/things_controller.rb'

# Three of them in one file is three: which relations carry the marker is the
# tree's choice, and the gate's floor is a count of relations, not of files.
expect_count 'strict_loading: three relations in one controller is three', <<~RUBY,
  class ThingsController
    def index
      @a = A.strict_loading.includes(:x)
      @b = B.strict_loading.includes(:y)
      @c = C.strict_loading.includes(:z)
    end
  end
RUBY
             'strict_loading', 3, filename: 'app/controllers/things_controller.rb'

# The two ways an unconfined count could be met without marking a relation.
expect_count 'strict_loading: a call under spec/ is not a relation this app builds', <<~RUBY,
  describe Thing do
    it 'x' do
      Thing.strict_loading.to_a
    end
  end
RUBY
             'strict_loading', 0, filename: 'spec/models/thing_spec.rb'

expect_count 'strict_loading: the bang form on a loaded record is not the marker', <<~RUBY,
  class Thing
    def a(r)
      r.strict_loading!
    end
  end
RUBY
             'strict_loading', 0, filename: 'app/models/thing.rb'

# The near miss: 6.1 also has an application-wide switch, and a tree that set
# it would be doing something else -- it makes EVERY lazy load raise, including
# the ones in a spec that built a record and read an association.  The switch
# is an assignment and not a call to `strict_loading`, so it is not counted,
# and a tree that only flipped it satisfies nothing.
expect_count 'strict_loading: the application-wide switch is not a call', <<~RUBY,
  module App
    class Application < Rails::Application
      config.active_record.strict_loading_by_default = true
    end
  end
RUBY
             'strict_loading', 0, filename: 'config/application.rb'

expect_count 'strict_loading: a plain preload is not the marker', <<~RUBY, 'strict_loading', 0
  class ThingsController
    def index
      @things = Thing.includes(:other)
    end
  end
RUBY

# ar_base_connection -- the accessor Rails 7.2 asks an application to stop
# taking.  The fork-hook probe reads the one call site this application runs at
# boot; this rule reads the ones nothing runs.  Receiver-qualified, because
# `connection` on its own belongs to plenty of objects that are not the Active
# Record class, and deliberately blind to the three replacements.
expect_count 'ar_base_connection: ActiveRecord::Base.connection is a finding', <<~RUBY, 'ar_base_connection', 1
  class Thing
    def a
      ActiveRecord::Base.connection.disconnect!
    end
  end
RUBY

expect_count 'ar_base_connection: ApplicationRecord.connection is the same call', <<~RUBY, 'ar_base_connection', 1
  class Thing
    def a
      ApplicationRecord.connection.execute('select 1')
    end
  end
RUBY

expect_count 'ar_base_connection: the 7.2 replacements are clean', <<~RUBY, 'ar_base_connection', 0
  class Thing
    def a
      ActiveRecord::Base.connection_pool.disconnect!
      ActiveRecord::Base.lease_connection.disconnect!
      ActiveRecord::Base.connection_handler.clear_all_connections!
      ActiveRecord::Base.with_connection { |c| c.execute('select 1') }
    end
  end
RUBY

expect_count 'ar_base_connection: `connection` on another receiver is clean', <<~RUBY, 'ar_base_connection', 0
  class Thing
    def a(pool, client)
      pool.connection
      client.connection.close
    end
  end
RUBY

# annotate_legacy_api -- the retired gem's API.  Both names, because a tree
# half way through the swap has one without the other, and receiver-qualified,
# because the fork's own call is spelt on a different constant and
# `Rails.application.load_tasks` is the line every Rakefile in the world has.
expect_count 'annotate_legacy_api: Annotate.set_defaults is a finding', <<~RUBY, 'annotate_legacy_api', 1
  if Rails.env.development?
    Annotate.set_defaults('position_in_class' => 'before')
  end
RUBY

expect_count 'annotate_legacy_api: Annotate.load_tasks is a finding', <<~RUBY, 'annotate_legacy_api', 1
  Annotate.load_tasks
RUBY

expect_count 'annotate_legacy_api: the 2014 file is two findings', <<~RUBY, 'annotate_legacy_api', 2
  if Rails.env.development?
    require 'annotate'
    task :set_annotation_options do
      Annotate.set_defaults('position_in_class' => 'before', 'show_indexes' => 'true')
    end
    Annotate.load_tasks
  end
RUBY

expect_count 'annotate_legacy_api: the fork and the Rakefile line are clean', <<~RUBY, 'annotate_legacy_api', 0
  require 'annotate_rb'
  AnnotateRb::Core.load_rake_tasks
  Rails.application.load_tasks
  Thing.set_defaults(a: 1)
RUBY

# ... and through a dispatch method, which is the spelling a one-word edit
# would otherwise use to walk past the rule.  All three dispatch names, and
# the receiver still has to be `Annotate`.
expect_count 'annotate_legacy_api: reached through send', <<~RUBY, 'annotate_legacy_api', 3
  Annotate.send(:set_defaults, 'position_in_class' => 'before')
  Annotate.public_send(:load_tasks)
  Annotate.__send__('set_defaults', {})
RUBY

expect_count 'annotate_legacy_api: send on another receiver is clean', <<~RUBY, 'annotate_legacy_api', 0
  AnnotateRb.send(:set_defaults, a: 1)
  Rails.application.send(:load_tasks)
  Annotate.send(:something_else)
RUBY

expect_count 'secret_token: assignment target counted', <<~RUBY,
  Rails.application.config.secret_token = ENV['SECRET_TOKEN']
RUBY
             'secret_token', 1, filename: 'config/initializers/secret_token.rb'

expect_count 'secret_token: secret_key_base is clean', <<~RUBY,
  Rails.application.config.secret_key_base = ENV['SECRET_KEY_BASE']
RUBY
             'secret_token', 0, filename: 'config/initializers/secret_token.rb'

# ---------------------------------------------------------------------------
# suppressions
# ---------------------------------------------------------------------------

expect_count 'suppression: each site counted, not each file', <<~RUBY, 'suppression', 3
  class Thing
    def a # rubocop:disable Metrics/AbcSize
      1
    end

    def b # rubocop:todo Style/Documentation
      2
    end

    def c
      eval('1') # brakeman:ignore
    end
  end
RUBY

expect_count 'suppression: the word inside a string is not a suppression', <<~RUBY, 'suppression', 0
  class Thing
    BANNED = '# rubocop:disable Metrics/AbcSize'
    DOC = <<~TEXT
      Do not write # rubocop:disable in this project.
      # brakeman:ignore is also banned.
    TEXT
  end
RUBY

expect_count 'suppression: an ordinary comment is clean', <<~RUBY, 'suppression', 0
  class Thing
    # This method is long because the protocol is long.
    def a
      1
    end
  end
RUBY

expect_count 'suppression: a re-enable comment is not a suppression', <<~RUBY, 'suppression', 1
  class Thing
    # rubocop:disable Metrics/AbcSize
    def a
      1
    end
    # rubocop:enable Metrics/AbcSize
  end
RUBY

# ---------------------------------------------------------------------------
# ERB templates
# ---------------------------------------------------------------------------

check 'erb: a construct inside a template is found' do
  got = scan(<<~ERB, ['render_text'], filename: 'app/views/things/show.html.erb')
    <div>
      <%= render text: 'x' %>
    </div>
  ERB
  fail!('erb: a construct inside a template is found',
        "got #{got['counts']['render_text']}") if got['counts']['render_text'] != 1
  fail!('erb: a construct inside a template is found',
        "erb_scanned=#{got['erb_scanned']}") if got['erb_scanned'] != 1
end

check 'erb: rails trim markers do not become parse errors' do
  got = scan(<<~ERB, ['render_text'], filename: 'app/views/things/index.html.erb')
    <%- if true -%>
      <p><%= 1 %></p>
    <%- end -%>
  ERB
  fail!('erb: rails trim markers do not become parse errors',
        got['parse_errors'].inspect) if got['parse_errors'].any?
end

check 'erb: a finding carries the template line, not the compiled line' do
  got = scan(<<~ERB, ['render_text'], filename: 'app/views/things/show.html.erb')
    <div>
      <p>one</p>
      <p>two</p>
    </div>
    <%= render text: 'x' %>
  ERB
  lines = got['findings'].map { |f| f['line'] }
  fail!('erb: a finding carries the template line, not the compiled line',
        lines.inspect) if lines != [5]
end

check 'erb: the block form compiles' do
  # The reason the scanner does not use stdlib ERB.  If this regresses, five of this
  # repository's templates silently stop being scanned.
  got = scan(<<~ERB, ['attr_accessible'], filename: 'app/views/things/_form.html.erb')
    <%= form_for(:subscription, url: subscription_path) do |f| %>
      <%= f.hidden_field :location_id %>
      <%= f.submit 'Go' %>
    <% end %>
  ERB
  fail!('erb: the block form compiles', got['parse_errors'].inspect) if got['parse_errors'].any?
end

check 'erb: `<%= yield %>` in a layout compiles' do
  got = scan("<html><body><%= yield %></body></html>\n", ['attr_accessible'],
             filename: 'app/views/layouts/application.html.erb')
  fail!('erb: `<%= yield %>` in a layout compiles',
        got['parse_errors'].inspect) if got['parse_errors'].any?
end

check 'erb: a code tag ending in a comment stays aligned' do
  got = scan(<<~ERB, ['render_text'], filename: 'app/views/things/show.html.erb')
    <% x = 1 # why %>
    <p><%= x %></p>
    <%= render text: 'x' %>
  ERB
  lines = got['findings'].map { |f| f['line'] }
  fail!('erb: a code tag ending in a comment stays aligned',
        "#{lines.inspect} #{got['parse_errors'].inspect}") if lines != [3]
end

check 'erb: a `#` inside a string is not a trailing comment' do
  got = scan(<<~ERB, ['render_text'], filename: 'app/views/things/show.html.erb')
    <% klass = "col-#" %>
    <%= render text: klass %>
  ERB
  lines = got['findings'].map { |f| f['line'] }
  fail!('erb: a `#` inside a string is not a trailing comment',
        "#{lines.inspect} #{got['parse_errors'].inspect}") if lines != [2]
end

check 'erb: a suppression in an ERB comment tag is counted' do
  got = scan(<<~ERB, ['suppression'], filename: 'app/views/things/show.html.erb')
    <div>
      <%# erblint:disable DeprecatedClasses %>
    </div>
  ERB
  if got['counts']['suppression'] != 1 || got['findings'].first['line'] != 2
    fail!('erb: a suppression in an ERB comment tag is counted', got['findings'].inspect)
  end
end

check 'erb: ordinary template text is not a suppression' do
  got = scan(<<~ERB, ['suppression'], filename: 'app/views/things/show.html.erb')
    <p>Write # rubocop:disable in a comment and it counts.</p>
  ERB
  fail!('erb: ordinary template text is not a suppression',
        got['findings'].inspect) if got['counts']['suppression'] != 0
end

# ---------------------------------------------------------------------------
# parse failures are findings, not skips
# ---------------------------------------------------------------------------

check 'parse error: an unparseable file is reported' do
  got = scan("class Thing\n  def a\n", ['attr_accessible'])
  if got['parse_errors'].length != 1
    fail!('parse error: an unparseable file is reported', got['parse_errors'].inspect)
  end
end

check 'parse error: it does not silently zero the whole scan' do
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, 'app/models'))
    File.write(File.join(dir, 'app/models/broken.rb'), "class A\n  def x\n")
    File.write(File.join(dir, 'app/models/fine.rb'),
               "class B < ActiveRecord::Base\n  attr_accessible :n\nend\n")
    got = CVGate::Scanner.new(dir, ['attr_accessible']).run.to_h
    if got['counts']['attr_accessible'] != 1 || got['parse_errors'].length != 1
      fail!('parse error: it does not silently zero the whole scan', got.inspect)
    end
  end
end

# ---------------------------------------------------------------------------
# the walk itself
# ---------------------------------------------------------------------------

check 'walk: vendored source is scanned, not excluded' do
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, 'vendor/plugins/legacy/lib'))
    File.write(File.join(dir, 'vendor/plugins/legacy/lib/x.rb'),
               "class X < ActiveRecord::Base\n  attr_accessible :n\nend\n")
    got = CVGate::Scanner.new(dir, ['attr_accessible']).run.to_h
    if got['counts']['attr_accessible'] != 1
      fail!('walk: vendored source is scanned, not excluded', got.inspect)
    end
  end
end

check 'walk: .git is not scanned' do
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, '.git/hooks'))
    File.write(File.join(dir, '.git/hooks/x.rb'), "attr_accessible :n\n")
    got = CVGate::Scanner.new(dir, ['attr_accessible']).run.to_h
    if got['counts']['attr_accessible'] != 0 || got['files_scanned'] != 0
      fail!('walk: .git is not scanned', got.inspect)
    end
  end
end

check 'walk: extensionless Rakefile/Gemfile/config.ru are scanned' do
  Dir.mktmpdir do |dir|
    File.write(File.join(dir, 'Rakefile'), "attr_accessible :a\n")
    File.write(File.join(dir, 'Gemfile'), "attr_accessible :b\n")
    File.write(File.join(dir, 'config.ru'), "attr_accessible :c\n")
    got = CVGate::Scanner.new(dir, ['attr_accessible']).run.to_h
    if got['counts']['attr_accessible'] != 3
      fail!('walk: extensionless Rakefile/Gemfile/config.ru are scanned', got.inspect)
    end
  end
end

check 'walk: a dotfile directory is scanned' do
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, '.config'))
    File.write(File.join(dir, '.config/x.rb'), "attr_accessible :n\n")
    got = CVGate::Scanner.new(dir, ['attr_accessible']).run.to_h
    if got['counts']['attr_accessible'] != 1
      fail!('walk: a dotfile directory is scanned', got.inspect)
    end
  end
end

# ---------------------------------------------------------------------------
# secret_key_base_assignment
# ---------------------------------------------------------------------------
#
# The rule that has to tell an assignment from a read.  `ast.rb` counts
# `x.foo = 1` as a call to `foo`, so a `:call` rule here would fire on every
# *reader* of the key as well -- and reading `secret_key_base` back is a
# perfectly ordinary thing for an initializer or a view to do.

expect_count 'secret_key_base: every assignment form is counted', <<~RUBY, 'secret_key_base_assignment', 5
  Rails.application.config.secret_key_base = "a"
  App::Application.config.secret_key_base = ENV["SECRET_TOKEN"]
  config.secret_key_base ||= "b"
  config.secret_key_base &&= "c"
  secret_key_base = "a local, which is still an assignment"
RUBY

expect_count 'secret_key_base: a multiple assignment target is counted', <<~RUBY, 'secret_key_base_assignment', 1
  a, config.secret_key_base = 1, "x"
RUBY

expect_count 'secret_key_base: reads are not assignments', <<~RUBY, 'secret_key_base_assignment', 0
  key = Rails.application.secrets.secret_key_base
  other = config.secret_key_base
  raise "no secret" if Rails.application.config.secret_key_base.to_s.empty?
  puts secret_key_base
RUBY

expect_count 'secret_key_base: not in a comment, a string or a symbol', <<~RUBY, 'secret_key_base_assignment', 0
  # config.secret_key_base = "moved to config/secrets.yml"
  DOC = "config.secret_key_base = 'x'"
  KEYS = [:secret_key_base]
  params.permit(:secret_key_base)
RUBY

expect_count 'secret_key_base: assigning something else is not a finding', <<~RUBY, 'secret_key_base_assignment', 0
  config.secret_token = "a"
  config.secret_key_base_backup = "b"
  secrets = { secret_key_base: "c" }
RUBY

# ---------------------------------------------------------------------------
# dead_engine_ref
# ---------------------------------------------------------------------------
#
# Dropping a gem without dropping the constants it defined leaves a NameError
# behind in whatever loads them.  Both of these are in the baseline's routes
# and initializers.

expect_count 'dead_engine_ref: routes and initializers are counted', <<~RUBY, 'dead_engine_ref', 3, filename: 'config/routes.rb'
  Rails.application.routes.draw do
    mount RailsEmailPreview::Engine, at: "emails"
    mount StyleGuide::Engine => "/style-guide"
  end
  RailsEmailPreview.setup do |config|
    config.preview_classes = []
  end
RUBY

expect_count 'dead_engine_ref: not in a comment or a string', <<~RUBY, 'dead_engine_ref', 0, filename: 'config/routes.rb'
  # mount RailsEmailPreview::Engine, at: "emails"
  NOTE = "StyleGuide::Engine was removed with the gem"
  Rails.application.routes.draw do
  end
RUBY

expect_count 'dead_engine_ref: a differently named constant is not a finding', <<~RUBY, 'dead_engine_ref', 0
  class RailsEmailPreviewer
    STYLE_GUIDE = StyleGuideless
  end
RUBY

# ---------------------------------------------------------------------------
# the pristine path manifest
# ---------------------------------------------------------------------------
#
# The instrument for "do not delete the repository's own tests".  Its failure
# mode is the interesting one: a manifest that does not arrive must not read as
# a clean tree, which is why the two negative fixtures below are about the
# manifest and not about the tree.

def with_pristine_tree
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, 'spec/models'))
    FileUtils.mkdir_p(File.join(dir, 'app/models'))
    # The fixture spec declares an example, because `gutted_tests` judges every
    # manifest `*_spec.rb` on whether it still does -- and the example asserts
    # something, because `hollow_tests` judges it on that.  An intact tree has
    # to be clean on both readings for either negative fixture to mean anything.
    File.write(File.join(dir, 'spec/models/thing_spec.rb'),
               "describe Thing do\n  it 'is a thing' do\n" \
               "    expect(Thing.new).to be_a(Thing)\n  end\nend\n")
    File.write(File.join(dir, 'spec/spec_helper.rb'), "RSpec.configure { |c| c }\n")
    File.write(File.join(dir, 'app/models/thing.rb'), "class Thing\nend\n")
    manifest = File.join(dir, 'pristine.json')
    # Both fixture files have content of their own, so `tests_substantive`
    # is the whole of `tests` here; the file that shipped as comment and
    # nothing else has its own fixture below.
    File.write(manifest, JSON.pretty_generate(
                           'tests' => ['spec/models/thing_spec.rb', 'spec/spec_helper.rb'],
                           'tests_substantive' => ['spec/models/thing_spec.rb',
                                                   'spec/spec_helper.rb'],
                           'core' => ['app/models/thing.rb']))
    yield dir, manifest
  end
end

def pristine_scan(dir, manifest)
  CVGate::Scanner.new(dir, ['attr_accessible'], manifest).run.to_h
end

check 'pristine: an intact tree reports nothing missing' do
  with_pristine_tree do |dir, manifest|
    got = pristine_scan(dir, manifest)
    fail!('pristine: an intact tree reports nothing missing',
          "missing_tests=#{got['missing_tests'].inspect}") unless got['missing_tests'] == []
    fail!('pristine: an intact tree reports nothing missing',
          "missing_core=#{got['missing_core'].inspect}") unless got['missing_core'] == []
    fail!('pristine: an intact tree reports nothing missing',
          "pristine=#{got['pristine'].inspect}") unless got['pristine'] == manifest
  end
end

check 'pristine: an intact tree reports nothing gutted' do
  with_pristine_tree do |dir, manifest|
    got = pristine_scan(dir, manifest)
    fail!('pristine: an intact tree reports nothing gutted',
          got['gutted_tests'].inspect) unless got['gutted_tests'] == []
  end
end

check 'pristine: a spec truncated to its require is reported as gutted' do
  name = 'pristine: a spec truncated to its require is reported as gutted'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'), "require 'spec_helper'\n")
    got = pristine_scan(dir, manifest)
    fail!(name, got['gutted_tests'].inspect) unless got['gutted_tests'] == ['spec/models/thing_spec.rb']
    # Still at its path, so the existence guard sees nothing -- which is the
    # whole reason this one exists.
    fail!(name, got['missing_tests'].inspect) unless got['missing_tests'] == []
  end
end

check 'pristine: a spec emptied to nothing is reported as gutted' do
  name = 'pristine: a spec emptied to nothing is reported as gutted'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'), '')
    got = pristine_scan(dir, manifest)
    fail!(name, got['gutted_tests'].inspect) unless got['gutted_tests'] == ['spec/models/thing_spec.rb']
  end
end

check 'pristine: a spec whose examples are commented out is reported as gutted' do
  name = 'pristine: a spec whose examples are commented out is reported as gutted'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'),
               "describe Thing do\n  # it 'is a thing' do\n  # end\nend\n")
    got = pristine_scan(dir, manifest)
    # A commented-out example still reads as one here: the check is a cheap
    # tripwire against emptying, not a parse of RSpec.  Recorded so the limit
    # is on the record rather than assumed away.
    fail!(name, got['gutted_tests'].inspect) unless got['gutted_tests'] == []
  end
end

check 'pristine: only *_spec.rb is judged on its examples' do
  name = 'pristine: only *_spec.rb is judged on its examples'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/spec_helper.rb'), '')
    got = pristine_scan(dir, manifest)
    fail!(name, got['gutted_tests'].inspect) unless got['gutted_tests'] == []
  end
end

check 'pristine: a deleted spec is missing, not gutted' do
  name = 'pristine: a deleted spec is missing, not gutted'
  with_pristine_tree do |dir, manifest|
    FileUtils.rm_f(File.join(dir, 'spec/models/thing_spec.rb'))
    got = pristine_scan(dir, manifest)
    fail!(name, got['missing_tests'].inspect) unless got['missing_tests'] == ['spec/models/thing_spec.rb']
    fail!(name, got['gutted_tests'].inspect) unless got['gutted_tests'] == []
  end
end

check 'pristine: the shared-example forms count as examples' do
  name = 'pristine: the shared-example forms count as examples'
  with_pristine_tree do |dir, manifest|
    %w[its specify scenario it_behaves_like include_examples].each do |form|
      File.write(File.join(dir, 'spec/models/thing_spec.rb'),
                 "describe Thing do\n  #{form}(:x) { }\nend\n")
      got = pristine_scan(dir, manifest)
      fail!(name, "#{form}: #{got['gutted_tests'].inspect}") unless got['gutted_tests'] == []
    end
    # ... and a word that merely starts with one of them does not.
    File.write(File.join(dir, 'spec/models/thing_spec.rb'),
               "describe Thing do\n  iterate(:x) { }\n  items.each { }\nend\n")
    got = pristine_scan(dir, manifest)
    fail!(name, "iterate: #{got['gutted_tests'].inspect}") unless got['gutted_tests'] == ['spec/models/thing_spec.rb']
  end
end

check 'pristine: a deleted spec is reported by path' do
  with_pristine_tree do |dir, manifest|
    FileUtils.rm_rf(File.join(dir, 'spec'))
    got = pristine_scan(dir, manifest)
    want = ['spec/models/thing_spec.rb', 'spec/spec_helper.rb']
    fail!('pristine: a deleted spec is reported by path',
          got['missing_tests'].inspect) unless got['missing_tests'] == want
    fail!('pristine: a deleted spec is reported by path',
          "core also reported: #{got['missing_core'].inspect}") unless got['missing_core'] == []
  end
end

check 'pristine: a deleted model is reported separately from the tests' do
  with_pristine_tree do |dir, manifest|
    FileUtils.rm_f(File.join(dir, 'app/models/thing.rb'))
    got = pristine_scan(dir, manifest)
    fail!('pristine: a deleted model is reported separately from the tests',
          got['missing_core'].inspect) unless got['missing_core'] == ['app/models/thing.rb']
    fail!('pristine: a deleted model is reported separately from the tests',
          got['missing_tests'].inspect) unless got['missing_tests'] == []
  end
end

check 'pristine: adding files does not hide a deletion' do
  # The reason the manifest is paths and not a count: a tree that deletes the
  # whole spec suite and adds more files than it removed still scans more
  # files than it started with.
  with_pristine_tree do |dir, manifest|
    FileUtils.rm_rf(File.join(dir, 'spec'))
    FileUtils.mkdir_p(File.join(dir, 'spec/new'))
    5.times { |i| File.write(File.join(dir, "spec/new/extra_#{i}_spec.rb"), "x = #{i}\n") }
    got = pristine_scan(dir, manifest)
    fail!('pristine: adding files does not hide a deletion',
          got['missing_tests'].inspect) unless got['missing_tests'].length == 2
  end
end

check 'pristine: without the option nothing is looked for and nothing is claimed' do
  with_pristine_tree do |dir, _manifest|
    FileUtils.rm_rf(File.join(dir, 'spec'))
    got = CVGate::Scanner.new(dir, ['attr_accessible']).run.to_h
    # `pristine` is nil rather than absent so that "nothing missing" and
    # "nothing looked for" cannot be confused in a report.
    fail!('pristine: without the option nothing is looked for and nothing is claimed',
          "pristine=#{got['pristine'].inspect}") unless got['pristine'].nil?
    fail!('pristine: without the option nothing is looked for and nothing is claimed',
          got['missing_tests'].inspect) unless got['missing_tests'] == []
  end
end

check 'pristine: a manifest that is not there raises rather than reporting a clean tree' do
  name = 'pristine: a manifest that is not there raises rather than reporting a clean tree'
  with_pristine_tree do |dir, _manifest|
    begin
      CVGate::Scanner.new(dir, ['attr_accessible'], File.join(dir, 'nope.json'))
      fail!(name, 'no exception raised')
    rescue ArgumentError
      nil
    end
  end
end

check 'pristine: a manifest without the two lists raises' do
  name = 'pristine: a manifest without the two lists raises'
  with_pristine_tree do |dir, manifest|
    File.write(manifest, JSON.pretty_generate('tests' => ['spec/spec_helper.rb']))
    begin
      CVGate::Scanner.new(dir, ['attr_accessible'], manifest)
      fail!(name, 'a manifest with no `core` list was accepted')
    rescue ArgumentError
      nil
    end
  end
end

check 'pristine: a manifest without tests_substantive raises' do
  # A manifest without this list would otherwise report an empty
  # `emptied_tests` on any tree at all -- which reads as a pass.  Same failure mode as the two above, so the same
  # answer: raise, lose the run, do not score a clean tree.
  name = 'pristine: a manifest without tests_substantive raises'
  with_pristine_tree do |dir, manifest|
    File.write(manifest, JSON.pretty_generate('tests' => ['spec/spec_helper.rb'],
                                              'core' => ['app/models/thing.rb']))
    begin
      CVGate::Scanner.new(dir, ['attr_accessible'], manifest)
      fail!(name, 'a manifest with no `tests_substantive` list was accepted')
    rescue ArgumentError
      nil
    end
  end
end

# ---------------------------------------------------------------------------
# emptied_tests
# ---------------------------------------------------------------------------
#
# `gutted_tests` and `hollow_tests` judge `*_spec.rb` only, because an example
# declaration and an assertion are things only a spec file has.  On this
# repository that leaves 27 of the 78 shipped test paths -- factories,
# fixtures, helpers, the Teaspoon JavaScript -- judged by existence alone.
# This is the reading that asks those files for something.
#
# The fixture tree carries the awkward case deliberately: one manifest file
# that is comment and nothing else to begin with.  A rule that asked every
# shipped file for a substantive line would report it on the untouched tree,
# which is why the reading is against `tests_substantive` and why that is
# measured off the baseline rather than assumed.

def with_suite_tree
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, 'spec/javascripts'))
    FileUtils.mkdir_p(File.join(dir, 'spec/support'))
    FileUtils.mkdir_p(File.join(dir, 'app/models'))
    File.write(File.join(dir, 'spec/factories.rb'),
               "FactoryGirl.define do
  factory :thing
end
")
    File.write(File.join(dir, 'spec/support/fixture.csv'), "name,value\nx,1\n")
    # Comment and nothing else, exactly as `spec/javascripts/spec_helper.js`
    # ships in the repository.
    File.write(File.join(dir, 'spec/javascripts/spec_helper.js'),
               "// Teaspoon includes some support files\n//\n// and nothing else\n")
    File.write(File.join(dir, 'app/models/thing.rb'), "class Thing\nend\n")
    manifest = File.join(dir, 'pristine.json')
    tests = ['spec/factories.rb', 'spec/javascripts/spec_helper.js',
             'spec/support/fixture.csv']
    File.write(manifest, JSON.pretty_generate(
                           'tests' => tests,
                           'tests_substantive' => tests - ['spec/javascripts/spec_helper.js'],
                           'core' => ['app/models/thing.rb']))
    yield dir, manifest
  end
end

check 'emptied: an intact tree reports nothing emptied' do
  name = 'emptied: an intact tree reports nothing emptied'
  with_suite_tree do |dir, manifest|
    got = pristine_scan(dir, manifest)
    fail!(name, got['emptied_tests'].inspect) unless got['emptied_tests'] == []
  end
end

check 'emptied: a fixture truncated to nothing is reported' do
  name = 'emptied: a fixture truncated to nothing is reported'
  with_suite_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/factories.rb'), '')
    got = pristine_scan(dir, manifest)
    fail!(name, got['emptied_tests'].inspect) unless got['emptied_tests'] == ['spec/factories.rb']
    # None of the older readings sees it, which is the whole reason this one
    # exists: the path is there, and it is not a spec file.
    fail!(name, "missing=#{got['missing_tests'].inspect}") unless got['missing_tests'] == []
    fail!(name, "gutted=#{got['gutted_tests'].inspect}") unless got['gutted_tests'] == []
    fail!(name, "hollow=#{got['hollow_tests'].inspect}") unless got['hollow_tests'] == []
  end
end

check 'emptied: a file left with only comments or blanks is reported' do
  name = 'emptied: a file left with only comments or blanks is reported'
  with_suite_tree do |dir, manifest|
    [["# the factories used to be here\n", 'ruby comment'],
     ["\n   \n\t\n", 'whitespace'],
     ["// gone\n", 'js comment']].each do |body, what|
      File.write(File.join(dir, 'spec/factories.rb'), body)
      got = pristine_scan(dir, manifest)
      fail!(name, "#{what}: #{got['emptied_tests'].inspect}") unless
        got['emptied_tests'] == ['spec/factories.rb']
    end
  end
end

check 'emptied: a file that shipped as comment only is never reported' do
  # It is not in `tests_substantive`, so it has nothing to lose.  Without this
  # exclusion the untouched repository fails its own guard -- measured, not
  # hypothetical: `spec/javascripts/spec_helper.js` is the one file in 78.
  name = 'emptied: a file that shipped as comment only is never reported'
  with_suite_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/javascripts/spec_helper.js'), '')
    got = pristine_scan(dir, manifest)
    fail!(name, got['emptied_tests'].inspect) unless got['emptied_tests'] == []
  end
end

check 'emptied: a deleted file is missing and not emptied' do
  # The two readings must not double-count: deletion is `missing_tests`'
  # finding, and a file that is not there has not been emptied.
  name = 'emptied: a deleted file is missing and not emptied'
  with_suite_tree do |dir, manifest|
    FileUtils.rm_f(File.join(dir, 'spec/factories.rb'))
    got = pristine_scan(dir, manifest)
    fail!(name, "emptied=#{got['emptied_tests'].inspect}") unless got['emptied_tests'] == []
    fail!(name, "missing=#{got['missing_tests'].inspect}") unless
      got['missing_tests'] == ['spec/factories.rb']
  end
end

# ---------------------------------------------------------------------------
# the shipped-migration histogram
# ---------------------------------------------------------------------------
#
# `migrations.versions` counts every file under `db/migrate/`, which is every
# file a candidate puts there.  `migrations.shipped_versions` counts only the
# ones the manifest says arrived with the repository.  The difference is the
# whole point: the gate's sentence is about the migrations the application
# already had, and only the second reading is about those.

def with_migration_tree
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, 'db/migrate'))
    FileUtils.mkdir_p(File.join(dir, 'spec'))
    File.write(File.join(dir, 'spec/spec_helper.rb'), "RSpec.configure { |c| c }\n")
    shipped = %w[20130101000000_create_things.rb 20130102000000_add_name.rb]
    shipped.each_with_index do |file, i|
      File.write(File.join(dir, 'db/migrate', file),
                 "class M#{i} < ActiveRecord::Migration[4.2]\n  def change\n  end\nend\n")
    end
    manifest = File.join(dir, 'pristine.json')
    File.write(manifest, JSON.pretty_generate(
                           'tests' => ['spec/spec_helper.rb'],
                           'tests_substantive' => ['spec/spec_helper.rb'],
                           'core' => shipped.map { |f| "db/migrate/#{f}" }))
    yield dir, manifest
  end
end

check 'migrations: the shipped files are counted separately from the directory' do
  name = 'migrations: the shipped files are counted separately from the directory'
  with_migration_tree do |dir, manifest|
    got = pristine_scan(dir, manifest)['migrations']
    fail!(name, got.inspect) unless got['total'] == 2 && got['versions'] == { '4_2' => 2 }
    fail!(name, got.inspect) unless got['shipped_total'] == 2 &&
                                    got['shipped_versions'] == { '4_2' => 2 } &&
                                    got['shipped_unversioned'] == 0
  end
end

check 'migrations: dummy files beside the shipped ones do not answer for them' do
  # The attack the reading exists for.  Stamp the files the repository shipped
  # `[5.0]` -- which is the thing the gate is about, because the suffix picks
  # the shim their bodies run under -- and drop in as many `[4.2]` classes as
  # the count asks for.  Over the whole directory that is indistinguishable
  # from having done the work; over the shipped list it is not.
  name = 'migrations: dummy files beside the shipped ones do not answer for them'
  with_migration_tree do |dir, manifest|
    Dir.glob(File.join(dir, 'db/migrate/*.rb')).each do |file|
      File.write(file, File.read(file).sub('Migration[4.2]', 'Migration[5.0]'))
    end
    2.times do |i|
      File.write(File.join(dir, "db/migrate/2999000#{i}_dummy.rb"),
                 "class Dummy#{i} < ActiveRecord::Migration[4.2]\n  def change\n  end\nend\n")
    end
    got = pristine_scan(dir, manifest)['migrations']
    fail!(name, "directory: #{got['versions'].inspect}") unless got['versions']['4_2'] == 2
    fail!(name, "shipped: #{got['shipped_versions'].inspect}") unless
      got['shipped_versions'] == { '5_0' => 2 }
  end
end

check 'migrations: a deleted shipped file lowers the count rather than moving it' do
  # A file with no entry contributes to neither bucket, so deleting one is not
  # a way to turn a wrong version into an unversioned one -- and the count the
  # gate asks for goes down, which is the direction it has to go.
  name = 'migrations: a deleted shipped file lowers the count rather than moving it'
  with_migration_tree do |dir, manifest|
    FileUtils.rm_f(File.join(dir, 'db/migrate/20130101000000_create_things.rb'))
    got = pristine_scan(dir, manifest)['migrations']
    fail!(name, got.inspect) unless got['shipped_versions'] == { '4_2' => 1 }
    fail!(name, got.inspect) unless got['shipped_unversioned'] == 0
    fail!(name, "shipped_total=#{got['shipped_total'].inspect}") unless got['shipped_total'] == 2
  end
end

check 'migrations: without a manifest the shipped readings are absent, not zero' do
  # A predicate over an absent value is false, so a gate on it fails when the
  # manifest did not arrive.  Reported as nil rather than 0 so that the report
  # says "not looked for" and not "looked for and found none".
  name = 'migrations: without a manifest the shipped readings are absent, not zero'
  with_migration_tree do |dir, _manifest|
    got = CVGate::Scanner.new(dir, ['attr_accessible']).run.to_h['migrations']
    fail!(name, got.inspect) unless got['shipped_total'].nil? &&
                                    got['shipped_unversioned'].nil? &&
                                    got['shipped_versions'] == {}
    fail!(name, got.inspect) unless got['versions'] == { '4_2' => 2 }
  end
end

# `assertion_lines` and `hollow_tests`: the two readings of "the examples still
# assert something".  The first is a number over the whole tree, the second is
# the list of shipped spec files that are still there, still declare an example
# and have no assertion line of their own.
#
# The line is assembled from `Ripper.lex` tokens, and the tokens have to be
# joined with whitespace between them: concatenated with nothing between them,
# `should eq(200)` comes out `shouldeq(200)` and `\bshould\b` does not match
# it.  The repository's 2013 suite is mostly `x.should eq(...)`, so that mistake
# reads 212 lines where there are 401, and three spec files that assert on
# every example read as asserting on none -- while the floor still looks sane
# and the golden still clears it.  Hence fixtures of their own for both
# readings.

def with_two_specs
  with_pristine_tree do |dir, manifest|
    FileUtils.mkdir_p(File.join(dir, 'spec/models'))
    File.write(File.join(dir, 'spec/models/other_spec.rb'),
               "describe Other do\n  it 'x' do\n    expect(1).to eq(1)\n  end\nend\n")
    File.write(manifest, JSON.pretty_generate(
                           'tests' => ['spec/models/thing_spec.rb',
                                       'spec/models/other_spec.rb',
                                       'spec/spec_helper.rb'],
                           'tests_substantive' => ['spec/models/thing_spec.rb',
                                                   'spec/models/other_spec.rb',
                                                   'spec/spec_helper.rb'],
                           'core' => ['app/models/thing.rb']))
    yield dir, manifest
  end
end

check 'assertions: an intact tree asserts and is not hollow' do
  name = 'assertions: an intact tree asserts and is not hollow'
  with_pristine_tree do |dir, manifest|
    got = pristine_scan(dir, manifest)
    fail!(name, "assertion_lines=#{got['assertion_lines'].inspect}") unless got['assertion_lines'] == 1
    fail!(name, got['hollow_tests'].inspect) unless got['hollow_tests'] == []
  end
end

check 'assertions: the should syntax the repository uses is an assertion' do
  # The fixture for the token-joining mistake above.  Every form here is one
  # the repository's own suite actually contains; with the tokens welded
  # together this file reads as a single assertion line and as hollow.
  name = 'assertions: the should syntax the repository uses is an assertion'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'), <<~SPEC)
      describe Thing do
        it 'a' do
          response.status.should eq(200)
        end
        it 'b' do
          page.should have_content 'Learn more'
        end
        it 'c' do
          response_hash['resources'].should_not be_nil
        end
        its(:response) { should be_success }
        it 'd' do
          expect(Thing.new).to be_a(Thing)
        end
      end
    SPEC
    got = pristine_scan(dir, manifest)
    fail!(name, "assertion_lines=#{got['assertion_lines'].inspect}") unless got['assertion_lines'] == 5
    fail!(name, got['hollow_tests'].inspect) unless got['hollow_tests'] == []
  end
end

check 'assertions: a trailing comment does not buy a distinct line' do
  name = 'assertions: a trailing comment does not buy a distinct line'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'), <<~SPEC)
      describe Thing do
        it 'a' do
          expect(1).to eq(1) # one
          expect(1).to eq(1) # two
          expect(1).to eq(1) # three
        end
      end
    SPEC
    got = pristine_scan(dir, manifest)
    fail!(name, "assertion_lines=#{got['assertion_lines'].inspect}") unless got['assertion_lines'] == 1
  end
end

check 'assertions: a hash inside a string is not a comment' do
  name = 'assertions: a hash inside a string is not a comment'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'), <<~SPEC)
      describe Thing do
        it 'a' do
          expect(x).to eq('# one')
          expect(x).to eq('# two')
        end
      end
    SPEC
    got = pristine_scan(dir, manifest)
    fail!(name, "assertion_lines=#{got['assertion_lines'].inspect}") unless got['assertion_lines'] == 2
  end
end

check 'assertions: reindenting does not move the number' do
  name = 'assertions: reindenting does not move the number'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'),
               "describe Thing do\n  it 'a' do\n    expect(1).to eq(1)\n  end\nend\n")
    tight = pristine_scan(dir, manifest)['assertion_lines']
    File.write(File.join(dir, 'spec/models/thing_spec.rb'),
               "describe   Thing do\n        it 'a' do\n" \
               "                expect( 1 ).to   eq( 1 )\n  end\nend\n")
    loose = pristine_scan(dir, manifest)['assertion_lines']
    fail!(name, "tight=#{tight} loose=#{loose}") unless tight == 1 && loose == 1
  end
end

check 'assertions: the union does not count one line twice' do
  name = 'assertions: the union does not count one line twice'
  with_two_specs do |dir, manifest|
    body = "describe X do\n  it 'a' do\n    expect(1).to eq(1)\n  end\nend\n"
    File.write(File.join(dir, 'spec/models/thing_spec.rb'), body)
    File.write(File.join(dir, 'spec/models/other_spec.rb'), body)
    got = pristine_scan(dir, manifest)
    fail!(name, "assertion_lines=#{got['assertion_lines'].inspect}") unless got['assertion_lines'] == 1
  end
end

check 'assertions: a spec file the manifest never named is not counted' do
  name = 'assertions: a spec file the manifest never named is not counted'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/extra_spec.rb'),
               "describe Extra do\n  it 'a' do\n    expect(2).to eq(2)\n  end\nend\n")
    got = pristine_scan(dir, manifest)
    fail!(name, "assertion_lines=#{got['assertion_lines'].inspect}") unless got['assertion_lines'] == 1
    fail!(name, got['hollow_tests'].inspect) unless got['hollow_tests'] == []
  end
end

check 'hollow: a file that declares examples and asserts nothing is named' do
  name = 'hollow: a file that declares examples and asserts nothing is named'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'),
               "describe Thing do\n  it 'is a thing' do\n    Thing.new\n  end\nend\n")
    got = pristine_scan(dir, manifest)
    fail!(name, got['hollow_tests'].inspect) unless got['hollow_tests'] == ['spec/models/thing_spec.rb']
    # It is still at its path and still declares an example, so neither of the
    # other two readings sees anything -- which is the reason this one exists.
    fail!(name, got['missing_tests'].inspect) unless got['missing_tests'] == []
    fail!(name, got['gutted_tests'].inspect) unless got['gutted_tests'] == []
  end
end

check 'hollow: a gutted file is not also named hollow' do
  name = 'hollow: a gutted file is not also named hollow'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'), "require 'spec_helper'\n")
    got = pristine_scan(dir, manifest)
    fail!(name, got['gutted_tests'].inspect) unless got['gutted_tests'] == ['spec/models/thing_spec.rb']
    fail!(name, got['hollow_tests'].inspect) unless got['hollow_tests'] == []
  end
end

check 'hollow: a deleted file is missing, not hollow' do
  name = 'hollow: a deleted file is missing, not hollow'
  with_pristine_tree do |dir, manifest|
    FileUtils.rm_f(File.join(dir, 'spec/models/thing_spec.rb'))
    got = pristine_scan(dir, manifest)
    fail!(name, got['missing_tests'].inspect) unless got['missing_tests'] == ['spec/models/thing_spec.rb']
    fail!(name, got['hollow_tests'].inspect) unless got['hollow_tests'] == []
  end
end

check 'hollow: the count is met and the distribution is still named' do
  # The shape the tree-wide floor cannot see, and the whole argument for the
  # per-file guard: one file keeps every assertion in the suite, the other
  # keeps its examples and asserts nothing, and the union is as high as it ever
  # was.
  name = 'hollow: the count is met and the distribution is still named'
  with_two_specs do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'),
               "describe Thing do\n" +
               (1..40).map { |i| "  it 'a#{i}' do\n    expect(#{i}).to eq(#{i})\n  end\n" }.join +
               "end\n")
    File.write(File.join(dir, 'spec/models/other_spec.rb'),
               "describe Other do\n" +
               (1..40).map { |i| "  it 'b#{i}' do\n    Other.new\n  end\n" }.join +
               "end\n")
    got = pristine_scan(dir, manifest)
    fail!(name, "assertion_lines=#{got['assertion_lines'].inspect}") unless got['assertion_lines'] == 40
    fail!(name, got['gutted_tests'].inspect) unless got['gutted_tests'] == []
    fail!(name, got['hollow_tests'].inspect) unless got['hollow_tests'] == ['spec/models/other_spec.rb']
  end
end

check 'hollow: a spec that does not parse is both findings, not neither' do
  # `Ripper.lex` does not raise on a syntax error, it just returns the tokens
  # it got, so a truncated spec reaches this reading with no assertion on it
  # and is named hollow.  It is also a `parse_errors` finding.  Recorded rather
  # than assumed: the two readings overlap here, and the overlap is the safe
  # direction -- a file the guard could not read is not silently clean.
  name = 'hollow: a spec that does not parse is both findings, not neither'
  with_pristine_tree do |dir, manifest|
    File.write(File.join(dir, 'spec/models/thing_spec.rb'),
               "describe Thing do\n  it 'a' do\n")
    got = pristine_scan(dir, manifest)
    fail!(name, got['hollow_tests'].inspect) unless got['hollow_tests'] == ['spec/models/thing_spec.rb']
    files = got['parse_errors'].map { |e| e['file'] }
    fail!(name, got['parse_errors'].inspect) unless files == ['spec/models/thing_spec.rb']
  end
end

check 'walk: the same file is not counted twice by two globs' do
  Dir.mktmpdir do |dir|
    File.write(File.join(dir, 'config.ru'), "attr_accessible :c\n")
    got = CVGate::Scanner.new(dir, ['attr_accessible']).run.to_h
    if got['files_scanned'] != 1
      fail!('walk: the same file is not counted twice by two globs',
            "files_scanned=#{got['files_scanned']}")
    end
  end
end

check 'rules: an unknown rule name is refused' do
  begin
    CVGate::Scanner.new('/tmp', ['no_such_rule'])
    fail!('rules: an unknown rule name is refused', 'no exception raised')
  rescue ArgumentError
    nil
  end
end

check 'rules: every rule in the registry has a kind and a detail' do
  CVGate::RULES.each do |name, rule|
    fail!('rules: every rule in the registry has a kind and a detail',
          "#{name} has no kind") unless rule[:kind]
    fail!('rules: every rule in the registry has a kind and a detail',
          "#{name} has no detail") unless rule[:detail]
  end
end

check 'rules: a rule this scanner cannot evaluate is named, not zeroed' do
  name = 'rules: a rule this scanner cannot evaluate is named, not zeroed'
  Dir.mktmpdir do |dir|
    FileUtils.mkdir_p(File.join(dir, 'app/assets/javascripts'))
    File.write(File.join(dir, 'app/assets/javascripts/application.js'),
               "//= require jquery\n")
    got = CVGate::Scanner.new(dir, %w[sprockets_directive attr_accessible]).run.to_h
    # The asset directive is `assets.rb`'s to count.  What must not happen is
    # a `counts` entry of 0 for it: a profile gating that path would be
    # gating a constant, passing on every tree for nothing, and no log line
    # anywhere would say so.
    if got['counts'].key?('sprockets_directive')
      fail!(name, "sprockets_directive is in counts as #{got['counts']['sprockets_directive'].inspect}")
    end
    unless got['not_evaluated'] == ['sprockets_directive']
      fail!(name, "not_evaluated=#{got['not_evaluated'].inspect}")
    end
    # and the rule that *is* evaluated still is, with its real zero.
    unless got['counts']['attr_accessible'] == 0
      fail!(name, "attr_accessible=#{got['counts']['attr_accessible'].inspect}")
    end
    unless got['rules'] == %w[sprockets_directive attr_accessible]
      fail!(name, "rules=#{got['rules'].inspect}")
    end
  end
end

check 'rules: not_evaluated is empty when every rule was evaluated' do
  name = 'rules: not_evaluated is empty when every rule was evaluated'
  got = scan("attr_accessible :a\n", %w[attr_accessible before_filter])
  unless got['not_evaluated'] == []
    fail!(name, "not_evaluated=#{got['not_evaluated'].inspect}")
  end
  unless got['counts'] == { 'attr_accessible' => 1, 'before_filter' => 0 }
    fail!(name, "counts=#{got['counts'].inspect}")
  end
end

# ---------------------------------------------------------------------------
# form_for / form_with -- the pair that says "the forms were converted"
# ---------------------------------------------------------------------------

expect_count 'form_for: each call is counted', <<~RUBY, 'form_for', 2,
  module FormsHelper
    def subscription_form(&block)
      form_for(:subscription, url: subscription_path, &block)
    end

    def comment_form(&block)
      form_for([@location, Comment.new], &block)
    end
  end
RUBY
                                               filename: 'app/helpers/forms_helper.rb'

expect_count 'form_for: a call in a template is counted too', <<~ERB, 'form_for', 1,
  <div class="row">
    <%= form_for(:subscription, url: subscription_path) do |f| %>
      <%= f.submit 'Go' %>
    <% end %>
  </div>
ERB
                                                              filename: 'app/views/locations/_form.html.erb'

expect_count 'form_for: not in a comment, a string, a symbol or a definition', <<~RUBY,
  module FormsHelper
    # form_for(:subscription) was replaced by the call below
    DOC = "form_for(:subscription)"
    REPLACED = [:form_for]

    def form_for(*) # a shim that is a definition, not a call
      raise NotImplementedError
    end

    def subscription_form(&block)
      form_with(model: :subscription, url: subscription_path, &block)
    end
  end
RUBY
             'form_for', 0, filename: 'app/helpers/forms_helper.rb'

# `form_tag` is the other builder 5.1 unifies, counted under the same name: a
# `form_for` rewritten as a `form_tag` is a form that is still not built by
# `form_with`, and the gate reads "none of the old", not "none of one of them".
expect_count 'form_for: form_tag counts under the same name', <<~ERB, 'form_for', 1,
  <%= form_tag(search_path, method: :get) do %>
    <%= text_field_tag :q %>
  <% end %>
ERB
             filename: 'app/views/searches/_form.html.erb'

expect_count 'form_for: the two old builders add up', <<~RUBY, 'form_for', 2,
  module FormsHelper
    def subscription_form(&block)
      form_for(:subscription, url: subscription_path, &block)
    end

    def search_form(&block)
      form_tag(search_path, method: :get, &block)
    end
  end
RUBY
             filename: 'app/helpers/forms_helper.rb'

expect_count 'form_with: each call is counted', <<~ERB, 'form_with', 2,
  <%= form_with(model: :subscription, url: subscription_path, local: true) do |f| %>
    <%= f.submit 'Go' %>
  <% end %>
  <%= form_with(url: search_path, method: :get, local: true) do |f| %>
    <%= f.text_field :q %>
  <% end %>
ERB
                                                filename: 'app/views/locations/_form.html.erb'

expect_count 'form_with: the remote-forms switch is not a call to it', <<~RUBY,
  module CityVoice
    class Application < Rails::Application
      # form_with defaults to remote on 5.1; this turns that off.
      config.action_view.form_with_generates_remote_forms = false
      FORMS = "form_with"
    end
  end
RUBY
             'form_with', 0, filename: 'config/application.rb'

# ---------------------------------------------------------------------------
# load_defaults / load_defaults_5_1 -- the 5.1 declaration, read where it is
# written because 5.1 records nothing about it at runtime.  The scored reading
# is the positive one: on the tree this step is handed both read 0, and only
# the positive one moves when the declaration arrives.
# ---------------------------------------------------------------------------

expect_count 'load_defaults: the declaration is counted', <<~RUBY, 'load_defaults', 1,
  module CityVoice
    class Application < Rails::Application
      config.load_defaults 5.1
    end
  end
RUBY
                                                          filename: 'config/application.rb'

expect_count 'load_defaults: not in a comment or a string', <<~RUBY, 'load_defaults', 0,
  module CityVoice
    class Application < Rails::Application
      # config.load_defaults 5.1 would go here
      NOTE = "config.load_defaults 5.1"
    end
  end
RUBY
                                                           filename: 'config/application.rb'

expect_count 'load_defaults_5_1: the float spelling counts', <<~RUBY,
  module CityVoice
    class Application < Rails::Application
      config.load_defaults 5.1
    end
  end
RUBY
             'load_defaults_5_1', 1, filename: 'config/application.rb'

expect_count 'load_defaults_5_1: the string spelling counts too -- Rails takes either',
             <<~RUBY,
               module CityVoice
                 class Application < Rails::Application
                   config.load_defaults "5.1"
                 end
               end
             RUBY
             'load_defaults_5_1', 1, filename: 'config/application.rb'

expect_count 'load_defaults_5_1: an older release does not count', <<~RUBY,
  module CityVoice
    class Application < Rails::Application
      config.load_defaults 5.0
    end
  end
RUBY
             'load_defaults_5_1', 0, filename: 'config/application.rb'

# The three trees the gate has to separate, and the reason the reading is the
# match rather than the complement: `load_defaults 5.0` and no declaration at
# all both read 0 here, so both cost the same three points, and neither costs
# the whole family.
expect_count 'load_defaults_5_1: a version this instrument cannot read does not count',
             <<~RUBY,
               module CityVoice
                 class Application < Rails::Application
                   TARGET = 5.1
                   config.load_defaults TARGET
                 end
               end
             RUBY
             'load_defaults_5_1', 0, filename: 'config/application.rb'

expect_count 'load_defaults_5_1: no declaration at all reads the same 0 as the wrong one',
             <<~RUBY,
               module CityVoice
                 class Application < Rails::Application
                   config.active_record.belongs_to_required_by_default = true
                 end
               end
             RUBY
             'load_defaults_5_1', 0, filename: 'config/application.rb'

# The scope, both ways.  The declaration only does anything in `config/`, so a
# tree that writes the sentence into a spec has declared nothing -- and a gate
# that counted it would be handing out three points for a string.
expect_count 'load_defaults_5_1: the same declaration outside config/ is not one',
             <<~RUBY,
               RSpec.describe 'the framework defaults' do
                 it 'adopts 5.1' do
                   config.load_defaults 5.1
                 end
               end
             RUBY
             'load_defaults_5_1', 0, filename: 'spec/framework_defaults_spec.rb'

expect_count 'load_defaults: the unscored count is scoped the same way',
             <<~RUBY,
               RSpec.describe 'the framework defaults' do
                 it 'adopts 5.1' do
                   config.load_defaults 5.1
                 end
               end
             RUBY
             'load_defaults', 0, filename: 'spec/framework_defaults_spec.rb'

expect_count 'load_defaults_5_1: an initializer is config/ too',
             <<~RUBY,
               Rails.application.config.load_defaults 5.1
             RUBY
             'load_defaults_5_1', 1, filename: 'config/initializers/defaults.rb'

# The dispatch spellings.  `config.send(:load_defaults, 5.1)` adopts exactly
# what `config.load_defaults 5.1` adopts, and a gate that read only the second
# would cost three points to a tree that had done the work.
expect_count 'load_defaults_5_1: reached through send, it is still the declaration',
             <<~RUBY,
               module CityVoice
                 class Application < Rails::Application
                   config.send(:load_defaults, 5.1)
                 end
               end
             RUBY
             'load_defaults_5_1', 1, filename: 'config/application.rb'

expect_count 'load_defaults_5_1: public_send and __send__ read the same',
             <<~RUBY,
               module CityVoice
                 class Application < Rails::Application
                   config.public_send(:load_defaults, 5.1)
                   config.__send__(:load_defaults, "5.1")
                 end
               end
             RUBY
             'load_defaults_5_1', 2, filename: 'config/application.rb'

expect_count 'load_defaults_5_1: send with the wrong release still does not count',
             <<~RUBY,
               module CityVoice
                 class Application < Rails::Application
                   config.send(:load_defaults, 5.0)
                 end
               end
             RUBY
             'load_defaults_5_1', 0, filename: 'config/application.rb'

expect_count 'load_defaults_5_1: send to some other method is not a declaration',
             <<~RUBY,
               module CityVoice
                 class Application < Rails::Application
                   config.send(:some_other_setting, 5.1)
                 end
               end
             RUBY
             'load_defaults_5_1', 0, filename: 'config/application.rb'

# ---------------------------------------------------------------------------
# framework_defaults_switch -- the generated per-switch file, by its contents
# ---------------------------------------------------------------------------

expect_count 'framework_defaults_switch: the generated body, under any name', <<~RUBY,
  Rails.application.config.action_controller.raise_on_unfiltered_parameters = true
  Rails.application.config.action_controller.per_form_csrf_tokens = true
  Rails.application.config.action_controller.forgery_protection_origin_check = true
  ActiveSupport.to_time_preserves_timezone = true
  Rails.application.config.active_record.belongs_to_required_by_default = true
  ActiveSupport.halt_callback_chains_on_return_false = false
  Rails.application.config.ssl_options = { hsts: { subdomains: true } }
RUBY
             'framework_defaults_switch', 7,
             filename: 'config/initializers/adopted_defaults.rb'

expect_count 'framework_defaults_switch: the declaration and a comment are clean', <<~RUBY,
  module CityVoice
    class Application < Rails::Application
      # "5.1" is the five settings the 5.0 branch sets plus
      # assets.unknown_asset_fallback = false.
      config.load_defaults 5.1
    end
  end
RUBY
             'framework_defaults_switch', 0, filename: 'config/application.rb'

# The switch 5.1 itself introduces is not one the 5.0 generated file listed, so
# writing it by hand does not move that file's contents anywhere and is not a
# finding here.  Whether the fallback is actually off is a runtime reading two
# gates away.
expect_count 'framework_defaults_switch: the switch 5.1 adds is not one the file listed',
             <<~RUBY,
               Rails.application.config.assets.unknown_asset_fallback = false
             RUBY
             'framework_defaults_switch', 0,
             filename: 'config/initializers/assets.rb'

expect_count 'framework_defaults_switch: reading a switch back is not assigning it', <<~RUBY,
  module CityVoice
    class Application < Rails::Application
      REPORT = Rails.application.config.ssl_options.inspect
    end
  end
RUBY
             'framework_defaults_switch', 0, filename: 'config/application.rb'

# ---------------------------------------------------------------------------
# mailer_positional -- the pre-5.1 calling convention, anywhere in the tree
# ---------------------------------------------------------------------------

expect_count 'mailer_positional: both actions on the class are findings', <<~RUBY,
  class NotificationMailerPreview
    def confirmation_email
      NotificationMailer.confirmation_email(location_subscription)
    end

    def weekly_activity
      NotificationMailer.weekly_activity(subscriber)
    end
  end
RUBY
             'mailer_positional', 2, filename: 'app/mailer_previews/notification_mailer_preview.rb'

expect_count 'mailer_positional: the parameterised call is clean', <<~RUBY,
  class SubscriptionConfirmer
    def confirmation_email
      NotificationMailer.with(location_subscription: location_subscription).confirmation_email
    end

    def weekly
      NotificationMailer.with(subscriber: subscriber).weekly_activity.deliver_now
    end
  end
RUBY
             'mailer_positional', 0, filename: 'app/models/subscription_confirmer.rb'

expect_count 'mailer_positional: the mailer\'s own definitions are not calls', <<~RUBY,
  class NotificationMailer < ApplicationMailer
    def confirmation_email
      mail(to: params[:location_subscription].email)
    end

    def weekly_activity
      mail(to: params[:subscriber].email)
    end
  end
RUBY
             'mailer_positional', 0, filename: 'app/mailers/notification_mailer.rb'

expect_count 'mailer_positional: a same-named call on another receiver is clean', <<~RUBY,
  class Thing
    def go
      confirmation_email(subscription)
      OtherMailer.weekly_activity(subscriber)
    end
  end
RUBY
             'mailer_positional', 0

# The same call spelled through a dispatch method.  The rule is about spellings
# of the same requirement, and `X.send(:confirmation_email, y)` is the pre-5.1
# convention typed differently.
expect_count 'mailer_positional: the action reached through send is the same finding',
             <<~RUBY,
               class Thing
                 def go
                   NotificationMailer.send(:confirmation_email, subscription)
                   NotificationMailer.public_send(:weekly_activity, subscriber)
                   NotificationMailer.__send__(:confirmation_email, subscription)
                 end
               end
             RUBY
             'mailer_positional', 3, filename: 'app/models/thing.rb'

expect_count 'mailer_positional: send of something else, or on someone else, is clean',
             <<~RUBY,
               class Thing
                 def go
                   NotificationMailer.send(:default_url_options)
                   OtherMailer.send(:confirmation_email, subscription)
                   send(:confirmation_email, subscription)
                 end
               end
             RUBY
             'mailer_positional', 0, filename: 'app/models/thing.rb'

# `with(...).send(:confirmation_email)` is the 5.1 convention: the receiver of
# the dispatch is what `with` returned, not the constant.
expect_count 'mailer_positional: dispatch after with is the new convention', <<~RUBY,
  class Thing
    def go
      NotificationMailer.with(location_subscription: sub).send(:confirmation_email)
    end
  end
RUBY
             'mailer_positional', 0, filename: 'app/models/thing.rb'

check 'rules: every rule has both a positive and a negative fixture' do
  # The registry is the list a step's verifier picks from.  A rule with only a
  # positive fixture is a rule that might be matching the whole world; a rule
  # with only a negative one has never been shown to fire at all.  The asset
  # backstop is excluded here because it does not parse Ruby -- it is
  # rehearsed by `selftest_tools.rb` against asset files instead.
  expected = CVGate::RULES.keys - ['sprockets_directive']
  no_pos = expected.reject { |r| $positive[r] }
  no_neg = expected.reject { |r| $negative[r] }
  fail!('rules: every rule has both a positive and a negative fixture',
        "no positive fixture: #{no_pos.join(', ')}") if no_pos.any?
  fail!('rules: every rule has both a positive and a negative fixture',
        "no negative fixture: #{no_neg.join(', ')}") if no_neg.any?
end

# ---------------------------------------------------------------------------

if $checks < MIN_CHECKS
  $failures << "only #{$checks} checks ran, expected at least #{MIN_CHECKS}"
end

if $failures.empty?
  puts "gate selftest ok: #{$checks} checks"
  exit 0
else
  $failures.each { |f| puts "FAIL #{f}" }
  puts "gate selftest: #{$failures.length} failures of #{$checks} checks"
  exit 1
end
