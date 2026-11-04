begin
  proc { |a0| }.call(1)
  p :no_error
rescue ArgumentError => e
  p e.class
end
begin
  lambda { |a0| }.call(1)
  p :no_error
rescue ArgumentError => e
  p e.class
end
