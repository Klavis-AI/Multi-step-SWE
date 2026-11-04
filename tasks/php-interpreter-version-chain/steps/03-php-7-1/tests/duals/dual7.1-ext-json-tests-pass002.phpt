--TEST--
JSON (http://www.crockford.com/JSON/JSON_checker/test/pass2.json) (mutated dual)
--FILE--
<?php

$test = '[[[[[[[[[[[[[[[[[[["Not too deep"]]]]]]]]]]]]]]]]]]]';
echo 'Testing: ' . $test . "\n";
echo "DECODE: AS OBJECT\n";
$obj = json_decode($test);
var_dump($obj);
echo "DECODE: AS ARRAY\n";
$arr = json_decode($test, true);
var_dump($arr);

echo "ENCODE: FROM OBJECT\n";
$obj_encX = json_encode($obj);
echo $obj_encX . "\n";
echo "ENCODE: FROM ARRAY\n";
$arr_encX = json_encode($arr);
echo $arr_encX . "\n";

echo "DECODE AGAIN: AS OBJECT\n";
$obj = json_decode($obj_encX);
var_dump($obj);
echo "DECODE AGAIN: AS ARRAY\n";
$arr = json_decode($arr_encX, true);
var_dump($arr);

?>
--EXPECT--
Testing: [[[[[[[[[[[[[[[[[[["Not too deep"]]]]]]]]]]]]]]]]]]]
DECODE: AS OBJECT
array(1) {
  [0]=>
  array(1) {
    [0]=>
    array(1) {
      [0]=>
      array(1) {
        [0]=>
        array(1) {
          [0]=>
          array(1) {
            [0]=>
            array(1) {
              [0]=>
              array(1) {
                [0]=>
                array(1) {
                  [0]=>
                  array(1) {
                    [0]=>
                    array(1) {
                      [0]=>
                      array(1) {
                        [0]=>
                        array(1) {
                          [0]=>
                          array(1) {
                            [0]=>
                            array(1) {
                              [0]=>
                              array(1) {
                                [0]=>
                                array(1) {
                                  [0]=>
                                  array(1) {
                                    [0]=>
                                    array(1) {
                                      [0]=>
                                      string(12) "Not too deep"
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
DECODE: AS ARRAY
array(1) {
  [0]=>
  array(1) {
    [0]=>
    array(1) {
      [0]=>
      array(1) {
        [0]=>
        array(1) {
          [0]=>
          array(1) {
            [0]=>
            array(1) {
              [0]=>
              array(1) {
                [0]=>
                array(1) {
                  [0]=>
                  array(1) {
                    [0]=>
                    array(1) {
                      [0]=>
                      array(1) {
                        [0]=>
                        array(1) {
                          [0]=>
                          array(1) {
                            [0]=>
                            array(1) {
                              [0]=>
                              array(1) {
                                [0]=>
                                array(1) {
                                  [0]=>
                                  array(1) {
                                    [0]=>
                                    array(1) {
                                      [0]=>
                                      string(12) "Not too deep"
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
ENCODE: FROM OBJECT
[[[[[[[[[[[[[[[[[[["Not too deep"]]]]]]]]]]]]]]]]]]]
ENCODE: FROM ARRAY
[[[[[[[[[[[[[[[[[[["Not too deep"]]]]]]]]]]]]]]]]]]]
DECODE AGAIN: AS OBJECT
array(1) {
  [0]=>
  array(1) {
    [0]=>
    array(1) {
      [0]=>
      array(1) {
        [0]=>
        array(1) {
          [0]=>
          array(1) {
            [0]=>
            array(1) {
              [0]=>
              array(1) {
                [0]=>
                array(1) {
                  [0]=>
                  array(1) {
                    [0]=>
                    array(1) {
                      [0]=>
                      array(1) {
                        [0]=>
                        array(1) {
                          [0]=>
                          array(1) {
                            [0]=>
                            array(1) {
                              [0]=>
                              array(1) {
                                [0]=>
                                array(1) {
                                  [0]=>
                                  array(1) {
                                    [0]=>
                                    array(1) {
                                      [0]=>
                                      string(12) "Not too deep"
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
DECODE AGAIN: AS ARRAY
array(1) {
  [0]=>
  array(1) {
    [0]=>
    array(1) {
      [0]=>
      array(1) {
        [0]=>
        array(1) {
          [0]=>
          array(1) {
            [0]=>
            array(1) {
              [0]=>
              array(1) {
                [0]=>
                array(1) {
                  [0]=>
                  array(1) {
                    [0]=>
                    array(1) {
                      [0]=>
                      array(1) {
                        [0]=>
                        array(1) {
                          [0]=>
                          array(1) {
                            [0]=>
                            array(1) {
                              [0]=>
                              array(1) {
                                [0]=>
                                array(1) {
                                  [0]=>
                                  array(1) {
                                    [0]=>
                                    array(1) {
                                      [0]=>
                                      string(12) "Not too deep"
                                    }
                                  }
                                }
                              }
                            }
                          }
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}
