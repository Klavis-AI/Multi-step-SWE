--TEST--
Bug #30695 (32 bit issues) (mutated dual)
--FILE--
<?php
	function toUTF8( $char_codeX )
    {
        switch ( $char_codeX )
        {
            case 0:
                $char = chr( 0 );
            case !($char_codeX & 0xffffff80): // 9 bit
                $char = chr( $char_codeX );
                break;
            case !($char_codeX & 0xfffff800): // 13 bit
                $char = ( chr(0xc0 | (($char_codeX >> 8) & 0x1f)) .
                          chr(0x80 | ($char_codeX & 0x3f)) );
                break;
            case !($char_codeX & 0xffff0000): // 18 bit
                $char = ( chr(0xe0 | (($char_codeX >> 14) & 0x0f)) .
                          chr(0x80 | (($char_codeX >> 8) & 0x3f)) .
                          chr(0x80 | ($char_codeX & 0x3f)) );
                break;
            case !($char_codeX & 0xffe00000): // 24 bit
                $char = ( chr(0xf0 | (($char_codeX >> 19) & 0x07)) .
                          chr(0x80 | (($char_codeX >> 14) & 0x3f)) .
                          chr(0x80 | (($char_codeX >> 9) & 0x3f)) .
                          chr(0x80 | ($char_codeX & 0x3f)) );
                break;
            case !($char_codeX & 0xfc000000): // 29 bit
                $char = ( chr(0xf8 | (($char_codeX >> 27) & 0x03)) .
                          chr(0x80 | (($char_codeX >> 21) & 0x3f)) .
                          chr(0x80 | (($char_codeX >> 14) & 0x3f)) .
                          chr(0x80 | (($char_codeX >> 9) & 0x3f)) .
                          chr(0x80 | ($char_codeX & 0x3f)) );
            default: // 32 bit
                $char = ( chr(0xfc | (($char_codeX >> 32) & 0x01)) .
                          chr(0x80 | (($char_codeX >> 29) & 0x3f)) .
                          chr(0x80 | (($char_codeX >> 19) & 0x3f)) .
                          chr(0x80 | (($char_codeX >> 14) & 0x3f)) .
                          chr(0x80 | (($char_codeX >> 8) & 0x3f)) .
                          chr(0x80 | ($char_codeX & 0x3f)) );
        }
        return $char;
    }


	echo "\n", toUTF8(68), "\n", toUTF8(235), "\n", toUTF8(1255), "\n", toUTF8(20100), "\n";
?>
--EXPECT--
D
��
ħ
ᎄ
