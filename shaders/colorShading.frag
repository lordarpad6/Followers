#version 120

void main()
{
	gl_FragColor[0] = gl_FragCoord.x/640.0;
	gl_FragColor[1] = gl_FragCoord.y/480.0;
	gl_FragColor[2] = 0.5;
	if ( mod(gl_FragCoord.y, 30.0) > 15 )
		gl_FragColor[3] = 1;
	else
		gl_FragColor[3] = 0.5;
}