#include "colorPot.h"

colorPot::colorPot(int board, int pin){
    colorPot.Board = board;
    colorPot.Pin = pin;
}

void colorPot::setCal(char color, int rgbw[4])
{
    switch(color) {
        case 'R':
            for(int i = 0; i<4; i++) {
                colorPot.redCal[i] = rgbw[i];
            }
            break;
        case 'G':
            for(int i = 0; i<4; i++) {
                colorPot.greenCal[i] = rgbw[i];
            }
            break;
        case 'B':
            for(int i = 0; i<4; i++) {
                colorPot.blueCal[i] = rgbw[i];
            }
            break;
        case 'Y':
            for(int i = 0; i<4; i++) {
                colorPot.yellowCal[i] = rgbw[i];
            }
            break;
        case 'O':
            for(int i = 0; i<4; i++) {
                colorPot.orangeCal[i] = rgbw[i];
            }
            break;
        case 'W':
            for(int i = 0; i<4; i++) {
                colorPot.whiteCal[i] = rgbw[i];
            }
            break;
        case 'E':
            for(int i = 0; i<4; i++) {
                colorPot.emptyCal[i] = rgbw[i];
            }
            break;
        default:
            return;
    }

}

char colorParse(int rgbwScan[4], colorSensorCal[28], int tolerance)
{
    // Initialize variables
    char colorchar = 'u';
    int red = 0;
    int green = 0;
    int blue = 0;
    int yellow = 0;
    int orange = 0;
    int white = 0;
    int empty = 0;
    int unknown = 0;
    int color = 0;


    for (int i = 0; i < 4; i++)
    { // Iterates through each scan value (RGBW)
        // Compares average value for channel to the toleranced calibration values for each color
        // If value falls within range it iterates the color variable
        // If all 3 readings match calibrated range then it is that color

        // Red
        if (colorPot.redCal[i] - tol <= rgbwScan[i] && rgbwScan[i] <= colorPot.redCal[i] + tol)
        {
            red += 1;
            color += 1;
        }

        // Green
        if (colorPot.greenCal[i] - tol <= rgbwScan[i] && rgbwScan[i] <= colorPot.greenCal[i] + tol)
        {
            green += 1;
            color += 1;
        }

        // Blue
        if (colorPot.blueCal[i] - tol <= rgbwScan[i] && rgbwScan[i] <= colorPot.blueCal[i] + tol)
        {
            blue += 1;
            color += 1;
        }

        // Yellow
        if (colorPot.yellowCal[i] - tol <= rgbwScan[i] && rgbwScan[i] <= colorPot.yellowCal[i] + tol)
        {
            yellow += 1;
            color += 1;
        }

        // Orange
        if (colorPot.orangeCal[i] - tol <= rgbwScan[i] && rgbwScan[i] <= colorPot.orangeCal[i] + tol)
        {
            orange += 1;
            color += 1;
        }

        // Brown
        if (colorPot.whiteCal[i] - tol <= rgbwScan[i] && rgbwScan[i] <= colorPot.whiteCal[i] + tol)
        {
            white += 1;
            color += 1;
        }

        // Empty
        if (colorPot.emptyCal[i] - (tol + 40) <= rgbwScan[i] && rgbwScan[i] <= colorPot.emptyCal[i] + (tol + 40))
        {
            empty += 1;
            color += 1;
        }

        // Unknown
        if (color == 0)
        {
            unknown += 1;
        }
        color == 0;
    }

    // Check which color fits all three readings of scan
    if (empty >= 4)
    {
        colorchar = 'e';
    }
    if (unknown >= 4)
    {
        // colorchar is initially set to 'u' so leave empty
    }
    if (red >= 4)
    {
        colorchar = 'r';
    }
    if (green >= 4)
    {
        colorchar = 'g';
    }
    if (blue >= 4)
    {
        colorchar = 'b';
    }
    if (yellow >= 4)
    {
        colorchar = 'y';
    }
    if (orange >= 4)
    {
        colorchar = 'o';
    }
    if (white >= 4)
    {
        colorchar = 'w';
    }

    // Return value
    return colorchar;
}