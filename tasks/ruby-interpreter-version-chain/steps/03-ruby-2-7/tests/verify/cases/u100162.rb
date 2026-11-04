$x = 'test';
    p(if $x == $x then true else false end)
    $bad = false
    unless $x == $x
      $bad = true
    end
    p(!$bad)
    p(unless $x != $x then true else false end)