using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Windows.Forms;

namespace Cnv
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();
        }

        private void forensics()
        {
            string sTab = "";
            foreach (byte b in System.Text.Encoding.UTF8.GetBytes(sTab.ToCharArray()))
            {
                int iChar = (int)b;
                Console.WriteLine(iChar.ToString());
            }
                    
        }
        private void button1_Click(object sender, EventArgs e)
        {
            // Emit Chinese New Version translation into BiblePay-QT

            // Credit goes to https://github.com/michaelchanwahyan/Bible Michael Chanwahyan for his text conversion of the CNV bible to source text files
            // The CNV, Chinese New Version Traditional, was a joint effort from 100 prominent Chinese Bible scholars, released in 1992.  https://www.biblegateway.com/versions/Chinese-New-Version-Traditional-CNVT/
            // All credit to Yeshua Hamashiach, for everything including our salvation.

            string sBooks = "GEN|EXO|LEV|NUM|DEU|JOS|JDG|RUT|SA1|SA2|KG1|KG2|CH1|CH2|EZR|NEH|EST|JOB|PSA|PRO|ECC|SOL|ISA|JER|LAM|EZE|DAN|HOS|JOE|AMO|OBA|JON|MIC|NAH|HAB|ZEP|HAG|ZEC|MAL|MAT|";
            sBooks += "MAR|LUK|JOH|ACT|PAU|CO1|CO2|GAL|EPH|PHI|COL|TH1|TH2|TI1|TI2|TIT|PLM|HEB|JAM|PE1|PE2|JO1|JO2|JO3|JDE|REV";
            string[] vBooks = sBooks.Split(new string[] { "|" }, StringSplitOptions.None);
            string sPath = "..\\..\\cnv\\";
            int iVerse = 0;
            string sData = String.Empty;
            char c1 = (char)227;
            string sTab = c1.ToString();

            for (int x = 0; x < 66; x++)
            {
                string sFile = sPath + vBooks[x];
                System.IO.StreamReader sr = new System.IO.StreamReader(sFile);
                while (sr.EndOfStream == false)
                {
                    string sLine = sr.ReadLine();
                    int iMaxLen = 8;
                    if (sLine.Length < 8) iMaxLen = sLine.Length;

                    string sPrefix = sLine.Substring(0, iMaxLen);
                    string sOrig = sLine.Substring(0, iMaxLen);
                    sPrefix = sPrefix.Replace(".", "|");
                    int iLoc = sPrefix.IndexOf(" ");
                    sPrefix = sPrefix.Insert(iLoc, "|");


                    sLine = sLine.Replace(sOrig, sPrefix);
                    sLine = sLine.Replace("  ", " ");
                    sLine = sLine.Replace(sTab, "");
                    sLine = sLine.Replace("　", "");
                    sLine = sLine.Replace("“", "'");
                    sLine = sLine.Replace("”", "'");
                    
                    string sFormatted = "     verse[" + iVerse.ToString() + "]=\"" + vBooks[x] + "|" + sLine + "~" + "\";";
                    iVerse++;
                    sData += sFormatted + "\r\n";
                }
                sr.Close();
                Console.WriteLine(sFile);
            }
            string sFileCNV = "..\\..\\cnv\\cnv.cpp";
            System.IO.StreamWriter sw = new System.IO.StreamWriter(sFileCNV);
            sw.Write(sData);
            sw.Close();
            Environment.Exit(0);

        }
    }
}
